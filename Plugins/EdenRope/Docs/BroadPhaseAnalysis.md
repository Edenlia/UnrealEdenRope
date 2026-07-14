# 碰撞检测 Broad Phase 分析：Chaos Physics vs Obi Physics

> 分析目标：理解两大物理引擎在刚体场景中如何构建空间加速结构，快速判断是否需要生成 Collision Constraint。

---

## 目录

1. [核心概念回顾](#1-核心概念回顾)
2. [Chaos Physics（UE5）Broad Phase](#2-chaos-physicsue5-broad-phase)
   - 2.1 空间加速结构层次
   - 2.2 场景构建流程
   - 2.3 碰撞对生成机制
   - 2.4 动态 vs 静态对象的差异处理
   - 2.5 ISpatialAcceleration 接口
   - 2.6 增量更新 vs 完全重建
   - 2.7 与 Narrow Phase / Constraint 的衔接
3. [Obi Physics（Unity）Broad Phase](#3-obi-physicsunity-broad-phase)
   - 3.1 Simplex 的概念
   - 3.2 LBVH 构建
   - 3.3 ObiColliderWorld：场景静态层
   - 3.4 GPU 后端
   - 3.5 自碰撞：空间哈希网格
   - 3.6 与 XPBD Constraint 的衔接
4. [横向对比](#4-横向对比)
5. [对 EdenRope 的启示](#5-对-edenrope-的启示)

---

## 1. 核心概念回顾

**碰撞检测三阶段流程：**

```
Broad Phase  →  Mid Phase / Narrow Phase  →  Constraint Generation
（快速剔除）       （精确接触计算）              （约束写入求解器）
```

- **Broad Phase**：用粗略的包围体（AABB）快速找出"可能碰撞的对"，代价是 O(N log N) 量级，目标是输出候选对列表（Candidate Pairs）。
- **Narrow Phase**：对每个候选对做精确的几何计算（GJK/EPA/SAT），输出真实的接触点、法线、穿透深度。
- **Constraint Generation**：基于接触数据创建 XPBD/PBD 碰撞约束。

本文重点是 **Broad Phase**。

---

## 2. Chaos Physics（UE5）Broad Phase

Chaos 的源码路径：`Engine/Source/Runtime/Experimental/Chaos/`

### 2.1 空间加速结构层次

Chaos 使用**可插拔的分层空间加速架构**，核心是三种结构：

#### 2.1.1 TAABBTree（静态/休眠对象的主结构）

```
Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/AABBTree.h
```

这是一个**自顶向下、SAH（Surface Area Heuristic）二叉 BVH**：

```cpp
template<typename TPayloadType, typename TLeafType, bool bMutable>
class TAABBTree : public ISpatialAcceleration<TPayloadType, FReal, 3>
{
    TArray<FAABBTreeNode> Nodes;       // 平铺节点数组（cache 友好）
    TArray<TLeafType>     Leaves;      // 叶子数据
    TArray<int32>         DirtyLeaves; // 仅 bMutable=true 时有效
    TAABB<FReal,3>        WorldBounds; // 缓存的根节点 AABB
    int32                 MaxChildrenInLeaf;
    int32                 MaxTreeDepth;
};
```

- `bMutable = false`：只读路径，性能最高，用于纯静态场景
- `bMutable = true`：支持脏标记，可增量更新叶节点
- 叶子可存单个对象或多个对象（桶，由 `MaxChildrenInLeaf` 控制）
- 构建使用**递归 SAH 分割**：沿方差最大的轴对重心坐标排序后二分

**节点 AABB 计算**：从叶子往上 union，每个内部节点的 AABB 是子节点 AABB 的并集：

```
InternalNode.AABB = Union(Child0.AABB, Child1.AABB)
```

#### 2.1.2 TAABBTreeDirtyGrid（动态对象层）

```
Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/AABBTreeDirtyGrid.h
```

专门处理**频繁移动的动态刚体**。它是在静态 AABBTree 之上叠加的**均匀空间哈希网格**：

| 对象类型           | 使用的结构          | 更新频率       |
|--------------------|---------------------|----------------|
| 静态 / Kinematic   | AABBTree (read-only)| 场景加载时构建 |
| 休眠的动态对象     | AABBTree (mutable)  | 唤醒事件触发   |
| 活跃的动态对象     | DirtyGrid           | 每帧           |

DirtyGrid 把世界空间划分为均匀网格，每个格子存对象索引。对动态对象的查询转变为格子枚举，而非树遍历。

#### 2.1.3 FSpatialAccelerationCollection（复合加速器）

```
Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/SpatialAccelerationCollection.h
```

这是实际使用的**复合结构**，内部持有：
- **静态 AABBTree**：用于 dynamic-vs-static 查询
- **DirtyGrid**：用于 dynamic-vs-dynamic 查询

---

### 2.2 场景构建流程

场景由 `FPBDRigidsEvolution`（或其子类 `FPBDRigidsEvolutionGBF`）管理，粒子以 **SoA（Structure of Arrays）** 格式存储：

```
FPBDRigidsSOAs
  ├── StaticParticles       // 静态 / Kinematic 刚体
  ├── DynamicParticles      // 活跃动态刚体
  ├── ClusteredParticles    // Geometry Collection 碎块
  └── DisabledParticles     // 休眠 / 禁用对象
```

**初始化时的构建流程：**

```
FPBDRigidsEvolution::AddConstraintsAndSolve()
  └── UpdateAccelerationStructures()
        └── ISpatialAcceleration::UpdateParticles()
              └── TAABBTree::Rebuild()
                    1. 计算每个叶节点的 AABB（对象几何包围盒）
                    2. 沿方差最大的轴对重心排序（SAH 启发）
                    3. 递归二分构建二叉树
                    4. 自底向上 union 计算内部节点 AABB
```

**Payload 类型**为 `FAccelerationStructureHandle`，封装 `(FParticleHandle*, ShapeIndex)`，允许查询回调直接识别命中的粒子及其形状。

---

### 2.3 碰撞对生成机制

**Broad Phase 查询由 `FPBDCollisionConstraints::UpdateBroadPhase()` 驱动：**

```
FPBDRigidsEvolution::AdvanceOneTimeStep()
  └── CollisionConstraints.UpdateConstraints()
        └── ISpatialAcceleration::OverlapAll() / SweepAll()
```

对每个**动态粒子**，执行 AABB 重叠查询：

```cpp
// 离散模式：
TAABB<FReal,3> InflatedBounds = Particle.WorldBounds().Thickened(CollisionMargin);
Accelerator->OverlapImp(InflatedBounds, [&](const FAccelerationStructureHandle& Payload) {
    EmitPotentialPair(DynamicParticle, Payload);
    return true; // 继续遍历
});

// CCD 模式（Sweep）：
TAABB<FReal,3> SweptBounds = Union(OldBounds, NewBounds);
Accelerator->SweepImp(SweptBounds, ...);
```

**树内部的遍历**使用**基于栈的 DFS**（避免递归，提升 cache 命中率）：

```
Push 根节点
while 栈非空:
    Pop 节点
    if 节点.AABB 与查询 AABB 重叠:
        if 叶节点: 输出候选对
        else: Push 左右子节点
```

复杂度：O(log N + k)，k 为命中候选对数量。

**候选对过滤：**
1. 自碰撞过滤（同一 Geometry Collection 的碎块互相跳过）
2. Collision Channel 响应矩阵检查
3. 距离阈值：分离距离 > `CollisionMargin + BoundsExpansion` → 丢弃

存活候选对存入 `TArray<FPotentialPair>`，键为 `(IndexA, IndexB)`。

---

### 2.4 动态 vs 静态对象的差异处理

**这是 Chaos Broad Phase 最重要的设计决策：两层查询分离。**

```
动态对象移动时：
  1. 更新其在 FPBDRigidsSOAs 中的 AABB
  2. 加入 DirtyGrid 的 DirtyElements 列表

下一帧 Broad Phase：
  ├── Dynamic-vs-Static：每个 dirty 对象查询静态 AABBTree
  └── Dynamic-vs-Dynamic：dirty 对象在 DirtyGrid 中查询相邻格子（3×3×3 邻域）
```

静态 AABBTree **从不在增量更新中被修改**，仅被查询。这使得多线程并发读取无锁。

当 dirty 元素数量超过阈值（`p.Chaos.AABBTree.DirtyElementsThreshold`，默认 ~1000）时，触发全量重建。

---

### 2.5 ISpatialAcceleration 接口

```
Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/SpatialAcceleration.h
```

该接口将 Broad Phase 算法与约束系统完全解耦：

```cpp
template <typename TPayloadType, typename T, int d>
class ISpatialAcceleration
{
public:
    // AABB 重叠查询（Broad Phase 核心）
    virtual bool OverlapImp(const TAABBi& AABB,
                            TSpatialVisitor<TPayloadType>& Visitor) = 0;

    // 扫掠查询（CCD）
    virtual bool SweepImp(const TAABBi& AABB,
                           const TVec3<T>& Dir, T Length,
                           TSpatialVisitor<TPayloadType>& Visitor) const = 0;

    // 射线查询（场景查询 / Raycast）
    virtual bool RaycastImp(const TVec3<T>& Start, const TVec3<T>& Dir,
                             T Length, T Thickness,
                             TSpatialVisitor<TPayloadType>& Visitor) const = 0;

    // 增量更新接口
    virtual void UpdateElement(const TPayloadType& Payload,
                                const TAABB<T,d>& NewBounds,
                                bool bHasBounds) = 0;
    virtual void RemoveElement(const TPayloadType& Payload) = 0;

    virtual ESpatialAcceleration GetType() const = 0;
};
```

**Visitor 模式**（`TSpatialVisitor`）避免了中间数组分配，允许内联处理和提前终止：

```cpp
template <typename TPayloadType>
struct TSpatialVisitor
{
    // 返回 true = 继续遍历；返回 false = 提前退出
    virtual bool VisitOverlap(const TSpatialVisitorData<TPayloadType>& Instance) = 0;
    virtual bool VisitSweep(const TSpatialVisitorData<TPayloadType>& Instance, ...) = 0;
};
```

---

### 2.6 增量更新 vs 完全重建

| 触发条件 | 操作 | 代价 |
|----------|------|------|
| 场景初始化 / Level Load | 全量重建 | O(N log N) |
| 对象状态切换（Dynamic↔Static） | 全量重建 | O(N log N) |
| Dirty 数量超阈值 | 全量重建 | O(N log N) |
| 动态对象每帧移动 | 增量：加入 DirtyGrid 脏列表 | O(k) per dirty object |

**叶节点失效时**（对象移出其节点的 AABB），Chaos **不立即向上重新拟合**，而是将其加入脏列表，延迟到下次重建。这避免了昂贵的自底向上 AABB refit 传播。

---

### 2.7 与 Narrow Phase / Constraint 的衔接

完整管道：

```
[Broad Phase]
FPBDCollisionConstraints::UpdateBroadPhase()
  → ISpatialAcceleration::OverlapImp()
  → 输出: TArray<FPotentialPair>

         ↓ Mid Phase（形状级过滤）

[Shape-Level Filter]  FParticlePairMidPhase
  → 多形状粒子的子 AABB 测试
  → Collision Filter 掩码检查
  → 休眠 / 禁用状态过滤

         ↓ Narrow Phase

[Narrow Phase]
  → 几何类型分发:
      Sphere-*   : 解析公式
      Box-Box    : SAT + 顶点裁剪
      Convex-*   : GJK + EPA
      Trimesh-*  : 子 BVH 遍历（BoundingVolumeHierarchy.h）
  → 输出: FContactPoint[] {穿透深度, 接触法线, 接触点}

         ↓ Constraint 注册

[Constraint Registration]
FPBDCollisionConstraints::AddConstraintsFromCollisions()
  → 每个接触流形创建 FPBDCollisionConstraint
  → 约束保存 (ParticleHandleA, ParticleHandleB, ContactPoints)
  → 跨帧保持约束对身份（用于 Warm Starting）

         ↓ 求解

[XPBD/PBD Solve]
FPBDRigidsEvolution::ApplyConstraints()
  → 位置级投影（PBD）或冲量修正（GBF）
```

**Warm Starting**：Chaos 在帧间保持约束状态，用上一帧的 Lagrange 乘子初始化当前帧，大幅提升堆叠场景的收敛速度。这要求 Broad Phase 的候选对通过 `(ParticleHandleA, ParticleHandleB)` 键持久追踪。

---

## 3. Obi Physics（Unity）Broad Phase

### 3.1 Simplex 的概念

Obi 的 Broad Phase **基本单元不是粒子，而是 Simplex（单纯形）**：

| Simplex 类型 | 几何含义 | 用途 |
|---|---|---|
| Simplex 0（点） | 单个粒子 | 布料顶点、绳端点 |
| Simplex 1（边） | 两粒子连线段 | **绳段**、布料边 |
| Simplex 2（面） | 三粒子三角形 | 布料面 |

每个 Simplex 的 AABB 是其所有粒子球包围盒的并集，并额外膨胀 `collisionMargin`（预测接触余量）：

```
AABB(simplex) = Union(sphere_AABB(particle_i))  for all i in simplex

sphere_AABB(particle_i) = [p_i - r_i,  p_i + r_i]  (各轴)

最终 AABB += collisionMargin（保守膨胀）
```

**绳子专项**：每个绳段对应一个 Simplex 1，其几何上等同于一根**胶囊体（Capsule）**，半径为粒子半径。锥形绳（粒子半径变化）的胶囊也会随之变化。

---

### 3.2 LBVH 构建（Morton Code BVH）

Obi 使用 **LBVH（Linear BVH）**，通过 Morton Code 实现 GPU 可并行构建：

```
1. 计算所有 Simplex AABB 中心点

2. 计算场景整体 AABB，将中心点归一化到 [0,1]³

3. 计算每个中心点的 Morton Code（Z-curve 空间填充曲线编码）

4. 按 Morton Code 对 Simplex 进行 Radix Sort
   → 排序后相邻索引的 Simplex 在空间上也相邻

5. 用排序顺序自底向上构建二叉树（并行 reduce）

6. 自底向上拟合内部节点 AABB（union）
```

**构建代价**：O(N)（排序后的线性构建），GPU 上可全并行。

相比 Chaos 的 SAH BVH（O(N log N)），LBVH 构建更快但查询质量略低（树不如 SAH 平衡）。LBVH 是为每帧重建优化的。

---

### 3.3 ObiColliderWorld：场景静态层

Obi 维护一个 **ObiColliderWorld** 单例，代表所有可碰撞几何体。每帧：

1. 所有注册的 `ObiCollider` 组件提交当前变换和形状数据。
2. 构建 `ColliderShape` 结构体的平铺列表：

```csharp
struct ColliderShape {
    AffineTransform transform;    // 世界变换
    ShapeType type;               // Sphere / Box / Capsule / Mesh / Heightfield
    float contactOffset;          // 向外膨胀量
    int rigidbodyIndex;           // -1 = 静态，否则指向 ObiRigidbody
}
```

3. 网格碰撞体（Mesh Collider）使用**预计算的三角形 BVH**（烘焙时构建，非运行时）。

4. 在所有 `ColliderShape` AABB 上构建**顶层 BVH**（每帧重建，或静态碰撞体增量更新）。

**Broad Phase 的两层结构：**

```
Level 1（顶层）：ObiColliderWorld BVH
                 → 管理场景中所有 ObiCollider 的 AABB

Level 2（底层）：Solver 内 Simplex LBVH
                 → 管理模拟中所有 Simplex 的 AABB

Broad Phase = 用 Level 2 的每个 Simplex 查询 Level 1 的 BVH
```

---

### 3.4 GPU 后端（Oni）

Obi 支持两套后端：

**CPU 后端（ObiCPUSolver）**：
- Unity Job System + Burst 编译
- BVH 遍历作为批量 CPU Job 执行
- 粒子自碰撞用并行空间哈希

**GPU 后端（ObiGPUSolver / Oni 插件）**：

| Compute Shader | 功能 |
|---|---|
| `GenerateMortonCodes.compute` | 为每个 Simplex 计算 Morton Code |
| `RadixSort.compute` | GPU 并行基数排序 |
| `BuildLBVH.compute` | 并行 LBVH 构建 |
| `TraverseLBVH.compute` | 每个 Simplex 遍历碰撞体 BVH |

GPU Broad Phase 输出：写入 GPU Buffer 的**候选 (Simplex, Collider) 对**平铺数组，直接送入 GPU 上的窄相（GJK/EPA Compute Shader）。

---

### 3.5 自碰撞：空间哈希网格

对于**同一 Solver 内的粒子自碰撞**，Obi 使用**空间哈希网格**而非 BVH：

```
网格格子大小 = 2 × max_particle_radius

对每个粒子：
  → 查询其 3×3×3 邻域格子
  → 对每个邻格内的粒子 j：
      if |p_i - p_j| < r_i + r_j + margin:
          emit (particle_i, particle_j)
```

**绳子自碰撞的特殊处理**：跳过沿绳拓扑距离小于阈值的段对（共享粒子的相邻段不参与）。

---

### 3.6 与 XPBD Constraint 的衔接

**完整的 Obi 每 Substep 流程：**

```
[Pre-step] 从 Unity Rigidbody 拉取速度/位置

[Predict] 预测位置: x_pred = x + v·dt + g·dt²

[Broad Phase] 基于 x_pred 构建 Simplex AABB（投机接触！）
  ├── 更新 LBVH 叶节点 AABB
  ├── 自底向上重新拟合内部节点
  └── 遍历 LBVH vs ObiColliderWorld BVH
      → 输出: candidate_pairs[]

[Narrow Phase] 对每个候选对 GJK/EPA
  → 输出: contacts[] {normal, depth, point, simplexBary}

[Filter] 移除: distance > contactOffset + margin

[XPBD 迭代 × N]
  ├── Distance 约束（绳段）
  ├── Bend 约束
  ├── Pin 约束
  └── Collision 约束:
      For each contact:
        w = invMass_particle + invMass_rigidbody(contact_point, normal)
        C = dot(x_pred - contact_point, normal) - r_particle
        if C < 0:  (穿透或在 margin 内)
          Δλ = -C / (w + α)     ← α=0 表示硬约束
          x_pred += Δλ · invMass · normal
          Δv_rb  -= Δλ · invMass_rb · normal   (累积)
          Δω_rb  -= Δλ · I_inv · cross(r, normal)

[速度更新]
  v = (x_pred - x) / dt
  x = x_pred

[摩擦] 切向速度阻尼（速度级，Coulomb 锥）

[Post-step] 将累积冲量推送给 Unity Rigidbody
  rigidbody.AddImpulse(Δv_rb × mass)
```

**投机接触（Speculative Contacts）**：Obi 用 `x_pred` 的 AABB 而非 `x` 的 AABB 做 Broad Phase。这相当于 CCD-lite：即使粒子尚未穿入，只要预测轨迹穿过碰撞体，接触也会被提前检测并解决，避免穿透。

---

## 4. 横向对比

| 维度 | Chaos Physics（UE5）| Obi Physics（Unity）|
|------|---------------------|---------------------|
| **主要加速结构** | SAH BVH（AABBTree）| LBVH（Morton Code）|
| **动态对象层** | DirtyGrid（均匀哈希）| 同一 LBVH 每帧重建 |
| **基本单元** | Particle + Shape（刚体几何）| Simplex（点/边/面）|
| **静态场景表示** | 只读 AABBTree（场景加载时构建）| ObiColliderWorld（平铺列表 + BVH）|
| **构建策略** | 静态树 SAH 全量构建；动态增量脏追踪 | 每帧全量 LBVH 重建（GPU 上 O(N)）|
| **静态重建代价** | O(N log N)，按需触发 | O(N)（LBVH），每帧触发 |
| **动态查询策略** | DirtyGrid 格子枚举 + 查静态树 | 遍历碰撞体 BVH |
| **自碰撞** | 粒子自身通过 PairFilter 排除 | 空间哈希网格（O(N·k)）|
| **投机接触** | CCD 通过 SweepBounds 实现 | 基于预测位置 x_pred 的 AABB（默认）|
| **GPU 支持** | Chaos Cloth GPU 有；RB 主要 CPU | 完整 GPU 管道（Oni 后端）|
| **Narrow Phase** | GJK+EPA / 解析 / 子 BVH | GJK+EPA / 解析（Burst 或 GPU）|
| **跨帧持久化** | 约束持久，Warm Starting | 无 Warm Starting（XPBD 每帧重新生成）|
| **碰撞合规** | 硬约束（默认）或 soft（配置）| 硬约束（α=0 默认）|
| **刚体耦合模式** | 引擎内部统一管理 | 预步拉取速度，后步推送冲量 |

**核心设计哲学差异：**

- **Chaos**：为**刚体主导**的场景优化，静态世界用高质量 SAH BVH，动态对象增量更新脏列表，避免每帧全量重建。优先保证大型复杂静态场景的查询性能。
- **Obi**：为**软体/粒子系统**优化，所有对象都是粒子，位置每帧完全变化，LBVH 用 GPU 并行 O(N) 重建摊销成本，不依赖增量追踪。

---

## 5. 对 EdenRope 的启示

### 5.1 当前 EdenRope 的 Broad Phase 方案

EdenRope 目前使用**暴力 O(N×M) 循环**：

```
XPBDEdenRopeEvolution::GenerateCollisionConstraints()
  → for each rope particle:
      for each registered collider:
          // 直接做 Narrow Phase 级别的距离测试
```

静态碰撞体在 `BeginPlay()` 时通过 `CollectStaticColliders()` 收集缓存，动态碰撞体每帧轮询。对于绳子（粒子数 10~100，碰撞体数 < 20）这是完全合理的。

### 5.2 何时需要引入 Broad Phase

若未来绳子粒子数量增加（> 500）或注册碰撞体增多（> 50），可以引入轻量级 Broad Phase：

**推荐方案：Capsule BVH（对齐 Obi 的绳子处理方式）**

```
构建阶段（每帧）：
  1. 将每个绳段表示为 Simplex 1（边 + 粒子半径 = 胶囊体）
  2. 计算每段的 AABB = Union(sphere(p_i, r_i), sphere(p_j, r_j))
  3. 用 Morton Code 对段 AABB 进行排序
  4. 构建 LBVH（O(N)，适合 CPU 线性构建）

查询阶段：
  1. 对每个注册碰撞体的 AABB
  2. 遍历绳段 LBVH 找出重叠的绳段集合
  3. 仅对重叠的绳段进入 Narrow Phase（距离测试）
```

### 5.3 预测位置（Speculative Contact）的重要性

两套引擎都强调：**Broad Phase 应该基于预测位置 `x_pred` 的 AABB，而非当前位置 `x` 的 AABB**。

EdenRope 当前在 `GenerateCollisionConstraints()` 中使用 `PredictedPositions` 做距离测试，这是正确的。若引入 Broad Phase，AABB 也应从 `PredictedPositions` 构建。

### 5.4 刚体冲量的累积时机

Obi 强调：**在所有 XPBD 迭代结束后，一次性将累积的 `Δv` 推送给刚体**，而不是每次迭代后立即推送。

EdenRope 的 `ApplyDeltasToUnrealRigidbodies()` 在步骤7（求解完成后）执行，这与 Obi 的模式一致，是正确的。

### 5.5 绳子自碰撞的空间哈希方案

当需要绳子自碰撞时（绳长较长、可能自缠绕），参考 Obi 的空间哈希方案：

```
格子大小 = 2 × max_particle_radius

对每个粒子 i：
  hash_index = Hash(floor(p_i / cell_size))
  查询 3×3×3 邻域中的所有粒子 j：
    if |p_i - p_j| < r_i + r_j + margin
    and 拓扑距离(i, j) >= skip_threshold:  // 跳过相邻段
        emit (i, j) 候选对
```

当前 `GenerateParticleCollisionConstraints()` 中的 O(N²) 暴力方法在 N > 200 时应替换为此方案。

---

## 参考资料

- Unreal Engine 5.4 源码：`Engine/Source/Runtime/Experimental/Chaos/`
  - `AABBTree.h / .cpp`
  - `AABBTreeDirtyGrid.h`
  - `SpatialAcceleration.h`
  - `PBDCollisionConstraints.h / .cpp`
  - `PBDRigidsEvolution.h / .cpp`
- Obi Physics 文档：https://obi.virtualmethodstudio.com/manual/
- XPBD 论文：Müller et al., *"Detailed Rigid Body Simulation with Extended Position Based Dynamics"*, 2020
- LBVH 论文：Lauterbach et al., *"Fast BVH Construction on GPUs"*, 2009
- SAH BVH：Wald, *"On fast Construction of SAH-based Bounding Volume Hierarchies"*, 2007
