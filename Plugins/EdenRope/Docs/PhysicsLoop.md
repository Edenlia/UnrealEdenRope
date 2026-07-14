# Physics Loop

## Part 1: Simulate Step

每次 `FXPBDEdenRopeEvolution::Simulate(DeltaTime)` 调用执行一个完整的 XPBD 模拟步。

### 流程

```text
Simulate(DeltaTime)
  │
  ├─ 0. 快照位置
  │     PreviousParticlePositions = ParticlePositions
  │     （在任何位置修改之前保存，用于渲染插值）
  │
  ├─ 0.5. 同步动态刚体
  │     UpdateDynamicRigidbodiesFromUnreal()
  │     从 Chaos 拉取刚体当前位置/速度/旋转
  │
  ├─ 1. 预测位置
  │     for each particle i:
  │       if InvMass[i] == 0:  → x_pred = x  （固定粒子）
  │       else:
  │         v += Gravity * dt
  │         x_pred = x + v * dt
  │
  ├─ 2. 生成动态碰撞约束
  │     根据预测位置检测碰撞，生成本帧的碰撞约束
  │
  ├─ 3. 重置
  │     ResetLambdas()                — XPBD Lambda 归零
  │     ResetCollisionData()          — 碰撞映射清空
  │     ResetDynamicRigidbodyDeltas() — 刚体速度增量归零
  │
  ├─ 4. 约束求解循环  ×SolverIterations
  │     所有约束依次调用 Project()
  │
  ├─ 5. 更新速度和位置
  │     v = (x_pred - x) / dt
  │     x = x_pred
  │
  ├─ 5.5. 全局速度阻尼
  │     v *= (1 - GlobalDamping)^dt    （指数衰减，帧率无关）
  │
  ├─ 6. 摩擦力
  │     对碰撞中的粒子，分解切向/法向速度后施加摩擦
  │
  └─ 7. 应用刚体增量
        ApplyDeltasToUnrealRigidbodies()
        将绳索对刚体的力反馈到 Chaos
```

### Key Files

| File | Description |
|------|-------------|
| `Private/Solver/XPBDEdenRopeEvolution.cpp` | 核心 Simulate 实现 |
| `Private/Constraints/*.cpp` | 各约束的 `Project()` 实现 |

---

## Part 2: Time Step

Solver Actor (`AEdenRopeSolverActor::Tick`) 管理外层时间步进，支持两种模式。

### Mode 1: Variable Timestep (`bUseFixedTimestep = false`)

直接将渲染帧的 DeltaTime（钳制后）传入 Simulate:

```text
Tick(DeltaTime)
  ClampedDeltaTime = min(DeltaTime, MaxPhysicsDeltaTime)
  InterpolationAlpha = 0
  Simulate(ClampedDeltaTime)
```

- 每渲染帧执行一次 Simulate
- 物理步长随帧率波动
- 无需插值，直接使用当前位置渲染
- 优势: 延迟低，实现简单
- 劣势: 帧率不稳定时物理行为不一致

### Mode 2: Fixed Timestep + Accumulation + Interpolation (`bUseFixedTimestep = true`)

物理以固定步长运行，渲染通过插值平滑。这是默认模式。

#### 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `FixedDeltaTime` | 1/60s | 固定物理步长 |
| `MaxPhysicsDeltaTime` | 1/30s | 输入 DeltaTime 上限 |
| `MaxSubsteps` | 8 | 每帧最大物理步数 |

#### 流程

```text
Tick(DeltaTime)
  │
  ├─ 1. 钳制输入
  │     ClampedDeltaTime = min(DeltaTime, MaxPhysicsDeltaTime)
  │
  ├─ 2. 累积时间
  │     AccumulatedTime += ClampedDeltaTime
  │
  ├─ 3. 计算步数
  │     NumSteps = floor(AccumulatedTime / FixedDeltaTime)
  │
  ├─ 4. Spiral-of-death 保护
  │     if NumSteps > MaxSubsteps:
  │       NumSteps = MaxSubsteps     （超出时间丢弃）
  │
  ├─ 5. 保留余量
  │     AccumulatedTime = fmod(AccumulatedTime, FixedDeltaTime)
  │     （剩余不足一个步长的时间留到下一帧）
  │
  ├─ 6. 计算插值 Alpha
  │     InterpolationAlpha = AccumulatedTime / FixedDeltaTime
  │     范围 [0, 1)：0 = 紧贴上一物理步，接近 1 = 接近下一物理步
  │
  └─ 7. 执行物理步
        for Step = 0 to NumSteps-1:
          Simulate(FixedDeltaTime)
```

#### 渲染插值

物理步和渲染帧不对齐，渲染帧通常落在两个物理步之间。用 `InterpolationAlpha` 在上一步和当前步位置之间线性插值:

```text
RenderPosition = Lerp(PreviousPosition, CurrentPosition, Alpha)
```

具体实现在 `UEdenRopeComponent::SendRenderDynamicData_Concurrent()`:

```cpp
const TArray<FVector>& PrevPositionsM = Solver->GetPreviousParticlePositions();
const float Alpha = Solver->GetInterpolationAlpha();

for (int32 i = 0; i < Count; ++i)
{
    const FVector InterpM = FMath::Lerp(PrevPositionsM[Idx], CurrentPositionsM[Idx], Alpha);
    CachedParticlePositions.Add(InterpM * 100.0f);  // m -> cm
}
```

`PreviousParticlePositions` 在每次 `Simulate()` 开始时快照 (Phase 0)，确保保存的是上一物理步完成后的位置。

#### 时间线示意

```text
渲染帧:   |-------- 16.7ms (60fps) --------|-------- 33.3ms (30fps) --------|
物理步:   |--16.7ms--|--16.7ms--|--16.7ms--|--16.7ms--|--16.7ms--|--16.7ms--|

帧1 (60fps, DeltaTime=16.7ms):
  AccumulatedTime = 16.7ms → NumSteps = 1, 余量 ≈ 0
  → 执行 1 次 Simulate, Alpha ≈ 0

帧2 (30fps, DeltaTime=33.3ms):
  AccumulatedTime = 33.3ms → NumSteps = 2, 余量 ≈ 0
  → 执行 2 次 Simulate, Alpha ≈ 0

帧3 (45fps, DeltaTime=22.2ms):
  AccumulatedTime = 22.2ms → NumSteps = 1, 余量 = 5.5ms
  → 执行 1 次 Simulate, Alpha = 5.5/16.7 ≈ 0.33
  → 渲染位置在上一步和当前步之间 33% 处
```

#### Spiral-of-Death 保护

当帧率严重下降时，累积时间会增长到需要大量物理步才能追平。不加限制会导致恶性循环（物理计算太多 → 帧率更低 → 累积更多）。

`MaxSubsteps` 设定上限（默认 8），超出的时间直接丢弃。物理会短暂减速但不会卡死。

### Key Files

| File | Description |
|------|-------------|
| `Private/Solver/EdenRopeSolverActor.cpp` | Tick 时间步进逻辑 |
| `Public/EdenRopeSolverConfiguration.h` | 时间步配置参数 |
| `Private/EdenRopeComponent.cpp` | 渲染插值实现 |
