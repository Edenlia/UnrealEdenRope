# Pin Constraint

XPBD Pin 约束，将绳子粒子附着到世界空间固定点或动态刚体上。

**实现文件**：`Private/Constraints/XPBDPinConstraint.cpp`

---

## 概述

Pin 约束在每次 `Project()` 调用（即求解器的每个迭代步）中执行以下操作：

1. 计算刚体的**当前接触点**（用于有效质量和冲量）
2. 用角速度积分得到**预测旋转**，再计算**预测目标位置**（用于约束梯度）
3. 根据约束违反量和有效质量，求解 **dLambda**
4. 将修正量分别写回粒子位置和刚体速度增量

两个位置量需严格区分（Obi 风格）：

| 变量 | 含义 | 用途 |
|------|------|------|
| `CurrentContactPos` | 当前帧接触点，不含预测 | 有效质量力矩臂、冲量施加点 |
| `TargetPos` | 预测到步末的目标位置 | 约束梯度 `C = P - TargetPos` |

用同一个预测位置同时做梯度和力矩臂，会导致力矩臂随每次迭代漂移（`DeltaLinearVelocity` 每迭代更新），有效质量在迭代间不一致，产生数值不稳定。

---

## 帧级上下文

`Project()` 在以下帧循环中被调用，每帧被调用 `SolverIterations`（默认 3）次：

```text
Simulate(dt)
  ├─ UpdateDynamicRigidbodiesFromUnreal()   ← RB.Velocity, Rotation, PivotPos 从 UE 拉取
  ├─ PredictPositions()                     ← P = x + (v + g*dt)*dt
  ├─ ResetLambdas()                         ← Lambda = 0
  ├─ ResetDynamicRigidbodyDeltas()          ← DeltaLinearVelocity = DeltaAngularVelocity = 0
  ├─ for iter in SolverIterations:
  │    PinConstraint.Project(...)           ← 本文档的内容
  └─ ApplyDeltasToUnrealRigidbodies()
```

每次 `Project()` 读到的 `RB.DeltaLinearVelocity` 已包含**本帧前几次迭代**累积的冲量，这是步内收敛机制的基础。

---

## Project() 逐步流程

### 输入数据

| 变量 | 类型 | 单位 | 说明 |
|------|------|------|------|
| `P` | `FVector&` | m | 粒子预测位置（可写） |
| `InvMass[i]` | `float` | 1/kg | 粒子逆质量 |
| `Lambda` | `float` | kg·m | 本帧累积 Lambda（帧初归零） |
| `RB.PivotPosition` | `FVector` | m | 刚体 pivot 世界坐标 |
| `RB.CenterOfMass` | `FVector` | m | 刚体质心世界坐标 |
| `RB.Rotation` | `FQuat` | — | 刚体当前帧旋转 |
| `RB.Velocity` | `FVector` | m/s | 刚体线速度 |
| `RB.AngularVelocity` | `FVector` | rad/s | 刚体角速度 |
| `RB.DeltaLinearVelocity` | `FVector` | m/s | 本帧已累积的线速度增量 |
| `RB.DeltaAngularVelocity` | `FVector` | rad/s | 本帧已累积的角速度增量 |
| `RB.InverseMass` | `float` | 1/kg | 刚体逆质量 |
| `RB.InverseInertiaTensor` | `FMatrix` | 1/(kg·m²) | 刚体逆惯性张量（世界空间） |
| `LocalOffset` | `FVector` | m | 附着点相对于 pivot 的局部偏移 |
| `Compliance` | `float` | m/N | 约束柔度（0 = 完全刚性） |
| `DeltaTime` | `float` | s | 当前物理步长 |

---

### Step 1 — 当前接触点

**输入**：`RB.PivotPosition`，`RB.Rotation`，`LocalOffset`

```
CurrentContactPos = RB.PivotPosition + RB.Rotation * LocalOffset
```

**输出**：`CurrentContactPos`（m，世界坐标）

> 使用**当前帧**的旋转，不含任何预测。此位置在本次 `Project()` 调用的生命期内固定不变，是数值稳定的力矩臂基准。

---

### Step 2 — 预测旋转

**输入**：`RB.Rotation`，`RB.AngularVelocity`，`RB.DeltaAngularVelocity`，`DeltaTime`

```
ω_pred = RB.AngularVelocity + RB.DeltaAngularVelocity

# 四元数运动学积分（一阶欧拉）
# q̇ = 0.5 * [ω, 0] * q
SpinDelta = FQuat(ω_pred.x * 0.5 * dt,
                  ω_pred.y * 0.5 * dt,
                  ω_pred.z * 0.5 * dt,
                  0)
SpinDeltaQ = SpinDelta * RB.Rotation

PredictedRotation = normalize(RB.Rotation + SpinDeltaQ)
```

**输出**：`PredictedRotation`（FQuat）

> `DeltaAngularVelocity` 包含本帧前几次迭代产生的角冲量，使旋转预测随约束求解过程更新。对 `LocalOffset = 0` 的球体无影响；对偏心附着点，若缺少此预测，`TargetPos` 会向错误方向偏移。

---

### Step 3 — 预测目标位置

**输入**：`RB.Velocity`，`RB.DeltaLinearVelocity`，`RB.CenterOfMass`，`RB.PivotPosition`，`RB.Rotation`，`PredictedRotation`，`LocalOffset`，`DeltaTime`

```
v_pred  = RB.Velocity + RB.DeltaLinearVelocity   # 质心速度

# Fix C: 先预测质心位置，再通过旋转后的局部偏移推导 Pivot
com_pred          = RB.CenterOfMass + v_pred * dt
local_com_to_pivot = Rotation⁻¹ * (RB.PivotPosition - RB.CenterOfMass)
pivot_pred        = com_pred + PredictedRotation * local_com_to_pivot

TargetPos = pivot_pred + PredictedRotation * LocalOffset
```

**输出**：`TargetPos`（m，世界坐标）

> **为什么不直接 `PivotPosition + v_pred * dt`？**
> UE Chaos 的 `GetLinearVelocity()` 返回的是**质心速度** $v_{COM}$，而非 pivot 速度。Pivot 的实际速度为：
> $$v_{pivot} = v_{COM} + \omega \times (pivot - COM)$$
> 直接将 $v_{COM}$ 加到 pivot 上会遗漏旋转贡献项 $\omega \times (pivot - COM) \cdot dt$，导致偏心附着点预测偏差。
>
> 正确做法：用 $v_{COM}$ 预测质心，通过预测旋转（已含 `DeltaAngularVelocity`，见 Step 2）将 COM→Pivot 的局部偏移旋转到新朝向，隐式地包含了旋转贡献项。
>
> 当 Pivot = COM（均匀球体）时，`local_com_to_pivot = 0`，行为与旧代码完全一致。
>
> 每次迭代时 `DeltaLinearVelocity` 已包含前次冲量，`TargetPos` 随迭代向粒子位置靠近，实现步内快速收敛。

---

### Step 4 — 约束违反量与梯度方向

**输入**：`P`，`TargetPos`

```
Gradient = P - TargetPos
C        = |Gradient|           (m，约束违反量，≥ 0)
n        = Gradient / C         (单位向量，约束梯度方向)
```

**输出**：`C`（m），`n`（GradientDir，无量纲）

> `C > 0` 表示粒子在 TargetPos 的 n 方向一侧，需要将粒子沿 -n 推向 TargetPos，同时将刚体沿 +n 推向粒子。若 `C < ε` 则约束已满足，直接返回。

---

### Step 5 — 粒子有效逆质量

**输入**：`InvMass[ParticleIndex]`

```
w_p = InvMass[ParticleIndex]    (1/kg)
```

**输出**：`w_p`

> 粒子的有效逆质量即其逆质量，无旋转惯量项。固定粒子（InvMass = 0）时 `w_p = 0`，约束对粒子无修正。

---

### Step 6 — 刚体有效逆质量

**输入**：`CurrentContactPos`，`RB.CenterOfMass`，`RB.InverseMass`，`RB.InverseInertiaTensor`，`n`

```
r         = CurrentContactPos - RB.CenterOfMass     (m，力矩臂)
AngAxis   = r × n                                   (m，旋转轴·力臂)
w_rb_ang  = AngAxis · (I⁻¹ · AngAxis)              (1/(kg·m²))
w_rb      = RB.InverseMass + w_rb_ang               (1/kg)
```

**输出**：`w_rb`（RBEffectiveW）

> **关键**：力矩臂 `r` 使用 `CurrentContactPos`（Step 1），而非 `TargetPos`。这使 `w_rb` 在本次 `Project()` 的三次迭代中保持一致，避免因 `DeltaLinearVelocity` 变化导致 `r` 漂移，保证有效质量矩阵的数值稳定性。

---

### Step 7 — 归一化柔度

**输入**：`Compliance`，`DeltaTime`

```
α̃ = Compliance / dt²           (1/(kg·m))
```

**输出**：`TildeAlpha`

> `α̃` 是时间步归一化后的柔度。`Compliance = 0` 时 `α̃ = 0`，约束完全刚性。

---

### Step 8 — 总有效逆质量

**输入**：`w_p`，`w_rb`，`TildeAlpha`

```
W_total = w_p + w_rb + α̃
```

**输出**：`W_total`（1/(kg·m)，分母）

---

### Step 9 — dLambda（无阻尼）

**输入**：`C`，`Lambda`，`TildeAlpha`，`W_total`

```
dLambda = (-C - α̃ * Lambda) / W_total
```

**输出**：`dLambda`（kg·m）

> `Lambda` 是本帧已累积的 Lambda，在首次迭代时为 0（已 ResetLambdas）。`α̃ * Lambda` 项随迭代增长，软化约束响应，防止过冲。

若启用 Potential Damping（Rayleigh 耗散）：

```
Gamma    = Compliance * DampingBeta / dt
dP       = P - OriginalPositions[i]              (步内位移)
SumVG    = n · dP
dLambda  = (-C - α̃ * Lambda - Gamma * SumVG) / ((1 + Gamma) * W_total)
```

---

### Step 10 — 累积 Lambda

**输入**：`Lambda`，`dLambda`

```
Lambda += dLambda
```

**输出**：`Lambda`（kg·m，本帧累积值）

---

### Step 11 — 粒子位置修正

**输入**：`P`，`w_p`，`dLambda`，`n`

```
ΔP = w_p * dLambda * n
P  = P + ΔP
```

**输出**：`P`（更新后的粒子预测位置，m）

> `dLambda < 0` 时（违反方向），修正量 `ΔP` 沿 `-n` 方向，粒子向 TargetPos 靠近。修正量大小与粒子质量成正比（质量越大，修正越小）。

---

### Step 12 — 刚体冲量

**输入**：`dLambda`，`n`，`DeltaTime`，`CurrentContactPos`，`RB.InverseMass`，`RB.InverseInertiaTensor`，`RB.CenterOfMass`

```
J = -dLambda * n / dt           (kg·m/s，冲量)

# 线速度增量
ΔV_linear = RB.InverseMass * J

# 角速度增量
r   = CurrentContactPos - RB.CenterOfMass
τ   = r × J                     (N·m·s，角冲量)
ΔV_angular = I⁻¹ · τ

# 累积到刚体
RB.DeltaLinearVelocity  += ΔV_linear
RB.DeltaAngularVelocity += ΔV_angular
```

**输出**：`RB.DeltaLinearVelocity`，`RB.DeltaAngularVelocity`（增量累积，m/s，rad/s）

> 冲量施加点同样使用 `CurrentContactPos`（与 Step 6 的力矩臂一致）。`J` 与粒子修正方向相反：粒子被推向 TargetPos，刚体被推向粒子。

---

## 数据流汇总

```text
帧初
  RB.{Velocity, AngularVelocity, PivotPos, Rotation}  ← UE Chaos
  RB.{DeltaLinearVelocity, DeltaAngularVelocity}       ← 归零
  Lambda                                               ← 归零

每次迭代（共 SolverIterations 次）
  │
  ├─ [Step 1]  CurrentContactPos = PivotPos + Rotation * LocalOffset
  │
  ├─ [Step 2]  ω_pred = Velocity_ang + Delta_ang
  │            PredictedRotation = normalize(q + 0.5*dt*ω_pred*q)
  │
  ├─ [Step 3]  v_pred = Velocity_lin + Delta_lin           (质心速度)
  │            com_pred = CenterOfMass + v_pred*dt
  │            local_cp = Rot⁻¹*(PivotPos-COM) → PredRot*local_cp → pivot_pred
  │            TargetPos = pivot_pred + PredictedRotation * LocalOffset
  │
  ├─ [Step 4]  C = |P - TargetPos|,  n = (P - TargetPos) / C
  │
  ├─ [Step 5]  w_p = InvMass[i]
  ├─ [Step 6]  r = CurrentContactPos - COM                 ← 固定力矩臂
  │            w_rb = 1/m + (r×n)·I⁻¹·(r×n)
  ├─ [Step 7]  α̃ = Compliance / dt²
  ├─ [Step 8]  W = w_p + w_rb + α̃
  │
  ├─ [Step 9]  dLambda = (-C - α̃*Λ) / W
  ├─ [Step 10] Lambda += dLambda
  │
  ├─ [Step 11] P += w_p * dLambda * n                      → 粒子修正
  └─ [Step 12] J = -dLambda*n/dt
               RB.Delta_lin += (1/m) * J                  → 刚体线速度增量
               RB.Delta_ang += I⁻¹ · (r × J)             → 刚体角速度增量

帧末
  RB.{Velocity, AngularVelocity} += RB.{Delta_lin, Delta_ang}  → 写回 UE Chaos
```

---

## 质量比与约束力充分性

绳子每步对刚体施加的速度增量（忽略旋转项，LocalOffset=0）：

```
ΔV_rb = (1/m_rb) * C / ((1/m_p + 1/m_rb) * dt)
       = C * m_p / ((m_p + m_rb) * dt)
```

球体每步受重力速度增量：

```
ΔV_gravity = g * dt ≈ 9.8 / 60 ≈ 0.163 m/s
```

约束力恰好平衡重力，要求绳子末端拉伸量：

```
C_eq = g * dt² * (m_p + m_rb) / m_p
```

| m_p（粒子） | m_rb（球体） | 所需拉伸 C_eq |
|------------|------------|--------------|
| 0.1 kg     | 1 kg       | ≈ 30 cm      |
| 0.5 kg     | 1 kg       | ≈ 6 cm       |
| 1 kg       | 1 kg       | ≈ 0.54 cm    |

**结论**：绳子末端粒子质量应与被附着刚体质量量级相当，否则约束力不足以支撑刚体。配置方式：在 `UEdenRopeComponent` 或 RopeSettings 中设置末端粒子 InvMass ≈ `1 / RigidBodyMass`。

---

## 关键文件

| 文件 | 说明 |
|------|------|
| `Private/Constraints/XPBDPinConstraint.h/.cpp` | Pin 约束实现（Project、ResetLambda） |
| `Private/RigidBodyProxy/RigidBodyProxy.h/.cpp` | `GetEffectiveInverseMass`、`AccumulateImpulse` 实现 |
| `Private/Solver/XPBDEdenRopeEvolution.cpp` | 帧循环，管理 Lambda 重置与约束迭代 |
| `Private/Components/RopePinComponent.cpp` | 注册约束，Tick 更新 WorldPosition |
