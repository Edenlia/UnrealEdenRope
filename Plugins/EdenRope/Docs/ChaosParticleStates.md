# Chaos Physics 粒子状态系统深度分析（UE5.4）

> 覆盖：粒子类型层次 / Mobility 映射 / 创建入口 / Kinematic AABB 更新 / 运行时状态切换 / 睡眠系统

---

## 目录

1. [粒子类型层次与 EObjectStateType](#1-粒子类型层次与-eobjectstatetype)
2. [Mobility 设置 → Chaos 粒子类型的映射](#2-mobility-设置--chaos-粒子类型的映射)
3. [粒子创建入口：从 UPrimitiveComponent 到 Chaos 粒子](#3-粒子创建入口从-uprimitive-component-到-chaos-粒子)
4. [Kinematic AABB 更新：骨骼动画 → 物理线程](#4-kinematic-aabb-更新骨骼动画--物理线程)
5. [运行时状态切换](#5-运行时状态切换)
6. [睡眠系统](#6-睡眠系统)

---

## 1. 粒子类型层次与 EObjectStateType

### 1.1 粒子类继承链

```
Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/ParticleHandle.h
Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/GeometryParticles.h
```

```
FGeometryParticle                          ← 基类：有碰撞几何，不可移动
  └── FKinematicGeometryParticle           ← 加入速度字段，外部驱动变换
        └── FPBDRigidParticle              ← 加入质量/惯量/力，完整物理模拟
```

每一层的 SoA 数组：

```cpp
// TGeometryParticles 存储（所有层共有）：
TArray<TUniquePtr<FImplicitObject>>  MGeometry;   // 碰撞几何
TArray<TRigidTransform<T,d>>         MX;           // 位置
TArray<TRotation<T,d>>               MR;           // 旋转

// TKinematicGeometryParticles 追加：
TArray<TVector<T,d>>                 MV;           // 线速度（由外部写入，用于推导）
TArray<TVector<T,d>>                 MW;           // 角速度

// TPBDRigidParticles 追加：
TArray<T>                            MM;           // 质量（标量）
TArray<PMatrix<T,d,d>>               MI;           // 惯量张量
TArray<TVector<T,d>>                 MF;           // 累积力
TArray<TVector<T,d>>                 MTorque;      // 累积力矩
TArray<EObjectStateType>             MObjectState; // 核心状态字段
TArray<int32>                        MIslandIndex; // 求解岛索引
```

### 1.2 EObjectStateType 枚举（全值）

```
Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/ObjectState.h
```

```cpp
namespace Chaos
{
    enum class EObjectStateType : int8
    {
        Uninitialized = 0,
        Static        = 1,   // 永不移动；在静态 AABBTree 中，零求解代价
        Kinematic     = 2,   // 外部驱动；在动态 BVH 中，每帧需更新 AABB
        Dynamic       = 3,   // 完整 XPBD 积分；参与岛管理、碰撞、约束求解
        Sleeping      = 4,   // 曾是 Dynamic，速度低于阈值后睡眠；仍在动态 BVH 中
        Count
    };
}
```

**关键设计**：Sleeping 粒子**不会**从动态 BVH 移出，它仍保留在 BVH 中，以便动态体与其发生 Broad Phase 命中时触发唤醒。

### 1.3 所有 Primitive 都是这三种粒子之一吗？

**是的**，每个启用了物理的 primitive 都对应恰好一个 Chaos 粒子：

| UE5 组件类型 | Chaos 粒子类型 |
|---|---|
| `UStaticMeshComponent`（Static 移动性） | `FGeometryParticle` |
| `UStaticMeshComponent`（Movable，不模拟） | `FKinematicGeometryParticle` |
| `UStaticMeshComponent`（Movable，模拟） | `FPBDRigidParticle` |
| `USkeletalMeshComponent` 每根骨骼（布娃娃） | 每骨一个 `FPBDRigidParticle` |
| `USkeletalMeshComponent` 每根骨骼（动画驱动） | 每骨一个 `FKinematicGeometryParticle` |
| `GeometryCollection`（碎块） | 每碎块一个 `FPBDRigidParticle`（由 `FGeometryCollectionPhysicsProxy` 管理）|
| `UPhysicsConstraintComponent` | 不是粒子，创建 `FJointConstraint` 约束对象 |
| Cloth | 走专用 Cloth Solver，不使用 SingleParticleProxy |

---

## 2. Mobility 设置 → Chaos 粒子类型的映射

```
Engine/Source/Runtime/Engine/Private/PhysicsEngine/BodyInstance.cpp
Engine/Source/Runtime/Engine/Private/Components/PrimitiveComponent.cpp
```

### 2.1 映射表

| UE5 Mobility | bSimulatePhysics | Chaos 粒子类型 | EObjectStateType |
|---|---|---|---|
| **Static** | false | `FGeometryParticle` | Static |
| **Stationary** | false | `FGeometryParticle` | Static |
| **Movable** | false | `FKinematicGeometryParticle` | Kinematic |
| **Movable** | true | `FPBDRigidParticle` | Dynamic |

> **注意**：**Stationary** 在物理上与 Static 完全等价。Stationary 是一个**光照概念**（接收静态光照烘焙但阻挡动态阴影），对 Chaos 没有任何额外含义。

### 2.2 `bSimulatePhysics` 与 Mobility 的门控关系

`bSimulatePhysics = true` 被 Mobility 门控：

```cpp
// UPrimitiveComponent::SetSimulatePhysics(bool bSimulate)
void UPrimitiveComponent::SetSimulatePhysics(bool bSimulate)
{
    BodyInstance.bSimulatePhysics = bSimulate;
    if (bSimulate && Mobility != EComponentMobility::Movable)
    {
        // 对 Static/Stationary Mesh 启用物理模拟是无效操作（引擎打 Warning）
        // 粒子类型不变，仍是 FGeometryParticle（Static）
    }
    RecreatePhysicsState();  // 销毁并重新创建物理体
}
```

**结论**：只有 Movable 组件才能在 Kinematic 和 Dynamic 之间切换。

### 2.3 `FBodyInstance::InitBody()` 中的决策树

```cpp
// 简化逻辑（BodyInstance.cpp）：
bool bIsDynamic = BodyInstance->ShouldInstanceSimulatingPhysics();
// ShouldInstanceSimulatingPhysics() = bSimulatePhysics && (Mobility == Movable)

if (bIsDynamic)
    FChaosEngineInterface::CreateDynamic(...)    // → FPBDRigidParticle
else if (Mobility == EComponentMobility::Movable)
    FChaosEngineInterface::CreateKinematic(...)  // → FKinematicGeometryParticle
else
    FChaosEngineInterface::CreateStatic(...)     // → FGeometryParticle
```

---

## 3. 粒子创建入口：从 UPrimitiveComponent 到 Chaos 粒子

### 3.1 顶层入口

```
Engine/Source/Runtime/Engine/Private/PhysicsEngine/BodyInstance.cpp
```

```cpp
void FBodyInstance::InitBody(
    UBodySetup*           Setup,      // 几何描述（碰撞形状、物理材质）
    const FTransform&     Transform,  // 初始世界变换
    UPrimitiveComponent*  PrimComp,   // 所属组件
    FPhysScene*           InRBScene,  // 物理场景
    FInitBodySpawnParams  const& SpawnParams)
```

### 3.2 完整调用链

```
[游戏线程]

UPrimitiveComponent::CreatePhysicsState()
  └── UPrimitiveComponent::OnCreatePhysicsState()
        └── FBodyInstance::InitBody(...)
              │
              ├── FPhysicsInterface::CreateActor(...)
              │
              ├── [根据类型选择创建路径]
              │     ├── FGeometryParticle::CreateParticle()   → Static
              │     └── FPBDRigidParticle::CreateParticle() → Kinematic or Dynamic, 会设置是Kinematic或Dynamic
              │           ↓
              │     FSingleParticlePhysicsProxy::Create(ParticleType)  ← 在此创建 Proxy
              │     BodyInstance.ActorHandle = Proxy  ← 游戏线程持有 Proxy
              │
              └── PhysScene->AddActorsToScene_AssumesLocked(...)
                    └── FChaosEngineInterface::AddActorToSolver(Handle, Solver)
                          └── FPBDRigidsSolver::RegisterObject(Proxy)
                                {
                                		AddDirtyProxy(InProxy);
                                		
                                		// 异步调用 ProcessSinglePushedData_Internal
                                		ProcessSinglePushedData_Internal()
                                		{
                                		
                                		}
                                    // [物理线程] 实际分配粒子 SoA 存储：
                                    Particles.CreateStaticParticles(1)   OR
                                    Particles.CreateKinematicParticles(1) OR
                                    Particles.CreateDynamicParticles(1)
                                }
```

### 3.3 FSingleParticlePhysicsProxy 的职责

```
Engine/Source/Runtime/Experimental/Chaos/Public/PhysicsProxy/SingleParticlePhysicsProxy.h
```

这是游戏线程与物理线程之间的**跨线程桥梁**：

```cpp
class FSingleParticlePhysicsProxy
{
    FParticleData      GameThreadState;  // GT 可安全读取的状态副本（插值用）
    FParticleDataPT*   PhysicsParticle;  // PT 侧的真实粒子指针
    FDirtyPropertiesManager DirtyFlags; // 跨帧脏标记队列
};

// 游戏线程读取接口（安全）：
FRigidBodyHandle_External& GetGameThreadAPI()

// 物理线程写入接口（仅在 PT 调用）：
FParticleDataPT* GetHandle_LowLevel()
```

跨线程数据同步在 `FPhysScene_Chaos::PushToPhysicsState()` 中完成（每帧 GT→PT 推送脏标记属性）。

---

## 4. Kinematic AABB 更新：骨骼动画 → 物理线程

Kinematic 对象（如动画驱动的骨骼）移动时，AABB 需要更新。以下是完整的调用链。

### 4.1 游戏线程侧（骨骼动画 → Proxy 脏标记）

```
USkeletalMeshComponent::TickPose()
  └── USkeletalMeshComponent::RefreshBoneTransforms()
        └── USkeletalMeshComponent::PerformAnimationEvaluation()
              └── FAnimationRuntime::FillComponentSpaceTransforms(...)
                    [骨骼变换更新完毕，存入 ComponentSpaceTransforms]

USkeletalMeshComponent::PostAnimEvaluation()
  └── USkeletalMeshComponent::UpdateKinematicBonesToAnim(
          const TArray<FTransform>& InSpaceBases, ...)
        └── FBodyInstance::SetBodyTransform(
                BoneTransform,
                ETeleportType::None,
                bMatchPrevTransform)
              └── FChaosEngineInterface::SetKinematicTarget(
                      ActorHandle,
                      TargetTransform,
                      ETeleportType)
                    └── FSingleParticlePhysicsProxy::SetKinematicTarget(Transform)
                          └── GameThreadState.KinematicTarget = NewTransform
                              DirtyFlags |= EPhysicsProxyDirtyFlags::KinematicTarget
```

**关键数据结构**：

```
Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/KinematicTargets.h
```

```cpp
struct FKinematicTarget
{
    TVector<FReal, 3>    Position;   // 本帧目标位置
    TRotation<FReal, 3>  Rotation;   // 本帧目标旋转
    EKinematicTargetMode Mode;       // None / Reset(瞬移) / Position(插值推导速度)
};
```

### 4.2 物理线程侧（KinematicTarget → AABB 更新）

```
[物理线程: FPhysScene_Chaos::StartFrame()]
  └── FPhysScene_Chaos::PushToPhysicsState()
        └── ForEach dirty proxy (KinematicTarget 脏的):
              FSingleParticlePhysicsProxy::PushToPhysicsState(...)
                └── FKinematicGeometryParticleHandle::SetKinematicTarget(NewTarget)
                    Evolution->SetParticleDirty(Particle)  ← 标记 AABB 需重算

[物理线程: FPBDRigidsEvolutionGBF::AdvanceOneTimeStep(dt)]
  └── FPBDRigidsEvolutionGBF::ApplyKinematicTargets(Dt, StepFraction)
        └── For each active kinematic with dirty KinematicTarget:
              // 1. 移动到目标位置（在 substep 内按 StepFraction 插值）
              Particle.SetX(KT.GetTarget().GetLocation())
              Particle.SetR(KT.GetTarget().GetRotation())

              // 2. 从位移推导速度（供碰撞响应使用）
              Particle.SetV((X_new - X_old) * InvDt)
              Particle.SetW(FRotation3::CalculateAngularVelocity(R_old, R_new, Dt))

              // 3. ← AABB 重算在这里发生
              Particle.UpdateWorldSpaceState(
                  FRigidTransform3(Particle.X(), Particle.R()),
                  BoundsExpansion)   // BoundsExpansion = v * dt（CCD 缓冲量）

              // 4. 标记动态 BVH 中该条目为脏
              BroadPhase.SetDynamicElementDirty(Particle.Handle())
```

**AABB 计算逻辑**（位于 `ParticleHandle.h` 或 `ImplicitObject.h`）：

```cpp
void UpdateWorldSpaceState(const FRigidTransform3& WorldTM, const FVec3& BoundsExpansion)
{
    // 遍历所有附属 FImplicitObject Shape
    // 每个 Shape: LocalAABB.TransformedAABB(WorldTM) → Union 到 WorldSpaceBounds
    WorldSpaceBounds = Geometry->BoundingBox().TransformedAABB(WorldTM);
    WorldSpaceBounds.Thicken(BoundsExpansion.GetAbsMax());
    // Thicken 量 = max(|v|) * dt，保证高速 Kinematic 不会被漏检
}
```

### 4.3 Kinematic 粒子在哪个空间加速结构里？

**Kinematic 粒子绝不在静态 AABBTree 中。**

```
Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/BroadPhase.h
Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsEvolutionGBF.cpp
```

```
FSpatialAccelerationCollection StaticAcceleration   ← 静态 AABBTree：仅 Static 粒子
FSpatialAccelerationCollection DynamicAcceleration  ← 动态 BVH：Kinematic + Dynamic + Sleeping
```

Kinematic 移动后：
1. `SetDynamicElementDirty()` 标记 DynamicAcceleration 中该条目为脏
2. 下一次 `UpdateBroadPhase()` 对脏节点进行 BVH refit（O(dirty_count)）
3. 静态 AABBTree **完全不动**

---

## 5. 运行时状态切换

### 5.1 可以运行时切换吗？

**完全可以**，Chaos 支持所有方向的状态切换：Dynamic ↔ Kinematic ↔ Static。

### 5.2 游戏线程 API

```cpp
// 最常用：在 Kinematic 和 Dynamic 之间切换
UPrimitiveComponent::SetSimulatePhysics(bool bSimulate);

// 更底层：直接控制 BodyInstance（可绕过 Mobility 检查）
FBodyInstance::SetInstanceSimulatePhysics(bool bSimulate, bool bMaintainPhysicsBlending);

// 强制改变 Mobility（通常会销毁重建物理状态，代价较高）
UPrimitiveComponent::SetMobility(EComponentMobility::Type NewMobility);
```

### 5.3 Chaos 内部的状态转换处理

```
Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsEvolutionGBF.cpp
```

核心函数：

```cpp
void FPBDRigidsEvolutionGBF::SetParticleObjectState(
    FPBDRigidParticleHandle* Particle,
    EObjectStateType NewState)
{
    EObjectStateType OldState = Particle->ObjectState();
    if (OldState == NewState) return;

    // 1. 更新粒子的状态字段
    Particle->SetObjectStateLowLevel(NewState);

    // 2. 处理求解岛归属
    if (NewState == EObjectStateType::Dynamic)
        IslandManager->AddToIsland(Particle);         // 加入（或创建）活跃岛
    else if (OldState == EObjectStateType::Dynamic
          || OldState == EObjectStateType::Sleeping)
        IslandManager->RemoveFromIsland(Particle);    // 离开岛

    // 3. 更新空间加速结构中的分类
    UpdateParticleInSpatialAcceleration(Particle, OldState, NewState);
}
```

### 5.4 空间加速结构的更新逻辑

```cpp
void FPBDRigidsEvolutionGBF::UpdateParticleInSpatialAcceleration(
    FGeometryParticleHandle* Particle,
    EObjectStateType OldState,
    EObjectStateType NewState)
{
    bool bWasStatic = (OldState == EObjectStateType::Static);
    bool bIsStatic  = (NewState == EObjectStateType::Static);

    if (bWasStatic && !bIsStatic)
    {
        // Static → Kinematic/Dynamic：从静态树移到动态 BVH
        StaticAcceleration->RemoveElement(Particle);
        DynamicAcceleration->AddElement(Particle, Particle->WorldSpaceInflatedBounds());
        bStaticTreeDirty = true;   // 触发静态 AABBTree 完全重建
    }
    else if (!bWasStatic && bIsStatic)
    {
        // Kinematic/Dynamic → Static：从动态 BVH 移到静态树
        DynamicAcceleration->RemoveElement(Particle);
        StaticAcceleration->AddElement(Particle, Particle->WorldSpaceInflatedBounds());
        bStaticTreeDirty = true;   // 触发静态 AABBTree 完全重建
    }
    else
    {
        // 同一个 BVH 内的切换（如 Dynamic ↔ Sleeping，Dynamic ↔ Kinematic）
        // 只标记脏，局部 refit，不重建整棵树
        DynamicAcceleration->SetElementDirty(Particle);
    }
}
```

### 5.5 各切换场景的代价总结

| 切换方向 | 空间结构操作 | 重建代价 |
|---|---|---|
| Dynamic ↔ Kinematic | 同在 DynamicAcceleration，仅脏标记 | O(1) 标记，O(k) refit |
| Dynamic ↔ Sleeping | 同在 DynamicAcceleration，仅脏标记 | O(1) |
| Static ↔ Kinematic | 跨结构迁移，触发 `bStaticTreeDirty` | **O(N log N) 全量重建** |
| Static ↔ Dynamic | 跨结构迁移，触发 `bStaticTreeDirty` | **O(N log N) 全量重建** |

> **实践建议**：在运行时频繁将 Static 对象切换为 Dynamic/Kinematic 代价极高（触发全量 AABBTree 重建）。如果对象未来可能移动，应从一开始就设置为 Movable（Kinematic），而不是运行时从 Static 切换。

---

## 6. 睡眠系统

### 6.1 睡眠触发条件

以下条件**全部**满足时，Dynamic 粒子（及其整个求解岛）进入睡眠：

1. 线速度 `|V|² < SleepLinearVelocityThreshold²`，连续 `SleepCounterThreshold` 帧
2. 角速度 `|W|² < SleepAngularVelocityThreshold²`，连续 `SleepCounterThreshold` 帧
3. 未被约束锁定（不受 pin 约束强制保持活跃）
4. **整个求解岛**（Island）内所有粒子均满足上述条件（岛级别睡眠）

> 重要：Chaos 是**岛级别睡眠**，不是单粒子级别。一个岛中只要有一个活跃粒子，整岛都不会睡眠。

### 6.2 睡眠阈值的定义位置

**CVar（全局默认值）**：

```
Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsSolver.cpp
```

```cpp
Chaos::FRealSingle GChaos_SleepLinearVelocityThreshold  = 1.0f;  // cm/s（外部单位）
Chaos::FRealSingle GChaos_SleepAngularVelocityThreshold = 1.0f;  // rad/s
int32              GChaos_SleepCounterThreshold          = 4;     // 连续帧数
```

**每个 Solver 的配置结构**：

```
Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/PBDRigidsSolver.h
```

```cpp
struct FChaosSolverSettings
{
    float SleepLinearVelocityThreshold;
    float SleepAngularVelocityThreshold;
    float SleepCouplingDeceleration;
    int32 SleepCounterThreshold;
};
```

**每个物体的覆盖**：通过 `UPhysicsMaterial` 的 `SleepLinearVelocityLimit` / `SleepAngularVelocityLimit` 属性覆盖全局阈值。

### 6.3 每帧检查睡眠的函数

```
Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsEvolutionGBF.cpp
```

调用时机：**每帧一次**（在 `AdvanceOneTimeStep()` 的末尾，而非每 substep）。

```cpp
void FPBDRigidsEvolutionGBF::UpdateSleepState()
{
    // 遍历所有活跃岛（Island = 接触/约束相连的粒子连通分量）
    for (FPBDIsland* Island : IslandManager->GetActiveIslands())
    {
        bool bAllSleepy = true;
        for (FPBDRigidParticleHandle* Particle : Island->GetParticles())
        {
            if (!ShouldSleep(Particle))
            {
                bAllSleepy = false;
                break;
            }
        }

        if (bAllSleepy)
        {
            // 整岛进入睡眠
            for (FPBDRigidParticleHandle* Particle : Island->GetParticles())
            {
                SetParticleObjectState(Particle, EObjectStateType::Sleeping);
                Particle->SetV(FVec3(0));   // 清零速度
                Particle->SetW(FVec3(0));
            }
            Island->SetSleeping(true);
            // 岛不再参与积分和约束求解
        }
    }
}

bool FPBDRigidsEvolutionGBF::ShouldSleep(FPBDRigidParticleHandle* Particle)
{
    const FReal LinVelSq  = Particle->V().SizeSquared();
    const FReal AngVelSq  = Particle->W().SizeSquared();
    const FReal LinThresh = SleepLinVelThreshold * SleepLinVelThreshold;
    const FReal AngThresh = SleepAngVelThreshold * SleepAngVelThreshold;

    if (LinVelSq < LinThresh && AngVelSq < AngThresh)
        Particle->IncrementSleepCounter();
    else
        Particle->ResetSleepCounter();  // 任意一帧超阈值，计数器清零

    return Particle->GetSleepCounter() >= SleepCounterThreshold;
}
```

### 6.4 睡眠时的空间加速结构变化

**睡眠粒子不会从动态 BVH 中移除。** 它依然保留在 `DynamicAcceleration` 里：

- 睡眠粒子仍会出现在 Broad Phase 查询结果中（被动态体的 AABB 查询命中）
- 当另一个动态体与其碰撞时，Narrow Phase 确认接触 → 生成约束 → 触发岛合并 → 唤醒

这样设计的原因：Sleeping 粒子的 AABB 是稳定的（速度为零，不再移动），保留在 BVH 中代价极低，但却是唤醒检测不可缺少的。

### 6.5 唤醒触发与函数

**触发唤醒的条件：**

| 触发源 | 机制 |
|---|---|
| 另一动态体与其接触 | Broad Phase 命中 → Narrow Phase 确认 → 岛合并 → 唤醒 |
| 外部 API 调用 | `FBodyInstance::WakeInstance()` |
| 施加力/冲量/速度 | `AddForce()` / `AddImpulse()` / `SetLinearVelocity()` |
| 新约束建立 | 创建连接到该睡眠体的约束（如 Pin） |

**唤醒函数**（物理线程）：

```
Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsEvolutionGBF.cpp
```

```cpp
void FPBDRigidsEvolutionGBF::WakeParticle(FPBDRigidParticleHandle* Particle)
{
    if (Particle->ObjectState() != EObjectStateType::Sleeping) return;

    SetParticleObjectState(Particle, EObjectStateType::Dynamic);
    Particle->ResetSleepCounter();

    // 唤醒整个岛（岛内所有粒子同时醒来）
    IslandManager->WakeIsland(Particle->Island());

    // 标记 BVH 条目为脏（下帧 refit，但粒子从未离开 BVH，所以是最小代价）
    SetParticleDirty(Particle);

    // 通知游戏线程（通过事件队列）
    SolverEventManager->EnqueueWakeEvent(Particle);
}

// 岛级别唤醒：
void FPBDIslandManager::WakeIsland(FPBDIsland* Island)
{
    Island->SetSleeping(false);
    for (FPBDRigidParticleHandle* P : Island->GetParticles())
    {
        P->SetObjectStateLowLevel(EObjectStateType::Dynamic);
        P->ResetSleepCounter();
        ActiveParticles.Add(P);   // 重新加入积分列表
    }
}
```

### 6.6 唤醒后的 Broad Phase 重新插入

由于睡眠粒子从未从 `DynamicAcceleration` 移出，唤醒后**不需要重新插入**：

1. `WakeParticle()` 调用 `SetParticleDirty()` → BVH 标记 refit（如果睡眠期间位置有变化，但通常不会）
2. 粒子被重新加入 `ActiveParticles` 视图 → 参与积分和碰撞查询
3. 代价：O(1) 标记操作

### 6.7 游戏线程的睡眠通知（事件与 Delegate）

**物理线程（事件入队）**：

```
Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsSolver.cpp
```

```cpp
FSolverEventFilters::FSleepEvent SleepEvent;
SleepEvent.Particle = Particle;
SleepEvent.bIsAwake = false;   // false=睡眠，true=唤醒
SolverEventManager->EnqueueSleepEvent(SleepEvent);
```

**游戏线程（读取并转发 Delegate）**：

```
Engine/Source/Runtime/Engine/Private/PhysicsEngine/PhysScene_Chaos.cpp
```

```cpp
void FPhysScene_Chaos::OnSolverSleepEvents(
    const TArray<FChaosParticleSleepData>& SleepData)
{
    for (const FChaosParticleSleepData& Event : SleepData)
    {
        FSingleParticlePhysicsProxy* Proxy = GetProxy(Event.Particle);
        FBodyInstance* BI = Proxy->GetBodyInstance();
        UPrimitiveComponent* Comp = BI->OwnerComponent.Get();

        if (Event.bIsAwake)
            Comp->OnComponentWake.Broadcast(Comp, Event.BoneName);
        else
            Comp->OnComponentSleep.Broadcast(Comp, Event.BoneName);
    }
}
```

**Blueprint 可绑定的 Delegate**：

```cpp
// 在 UPrimitiveComponent 中：
UPROPERTY(BlueprintAssignable)
FComponentSleepSignature OnComponentSleep;

UPROPERTY(BlueprintAssignable)
FComponentSleepSignature OnComponentWake;
```

---

## 关键文件速查表

| 主题 | 源文件路径 |
|---|---|
| 粒子类继承层次 | `Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/ParticleHandle.h` |
| SoA 粒子数据存储 | `Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/GeometryParticles.h` |
| `EObjectStateType` | `Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/ObjectState.h` |
| `FKinematicTarget` | `Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/KinematicTargets.h` |
| `FSingleParticlePhysicsProxy` | `Engine/Source/Runtime/Experimental/Chaos/Public/PhysicsProxy/SingleParticlePhysicsProxy.h` |
| `FBodyInstance::InitBody()` | `Engine/Source/Runtime/Engine/Private/PhysicsEngine/BodyInstance.cpp` |
| 粒子创建 / AddParticle | `Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsEvolutionGBF.cpp` |
| `ApplyKinematicTargets()` | `Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsEvolutionGBF.cpp` |
| `UpdateSleepState()` / `WakeParticle()` | `Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsEvolutionGBF.cpp` |
| 睡眠 CVar | `Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsSolver.cpp` |
| 岛管理 | `Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/IslandManager.h` |
| 游戏线程睡眠事件 | `Engine/Source/Runtime/Engine/Private/PhysicsEngine/PhysScene_Chaos.cpp` |
| 骨骼 → Kinematic 同步 | `Engine/Source/Runtime/Engine/Private/SkeletalMeshComponent.cpp` |
| 静态/动态 AABBTree | `Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/AABBTree.h` |
| BroadPhase 分层结构 | `Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/BroadPhase.h` |
