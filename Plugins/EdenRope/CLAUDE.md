# EdenRope Plugin

XPBD-based rope simulation plugin for UE 5.4+

## Units

UE单位: **CKS units** 

- 长度 [L]: Centimeter
- 质量 [M]: Kilogram
- 时间 [T]: Second

Eden Rope内部: **MKS units**

- 长度 [L]: Meter
- 质量 [M]: Kilogram
- 时间 [T]: Second

## Directory Structure

```
Source/EdenRope/
├── Public/                          # Public API
│   ├── EdenRopeSolverConfiguration.h  # FEdenRopeSolverConfiguration (USTRUCT)
│   ├── EdenRopeSettings.h             # UEdenRopeSettings (Project Settings)
│   └── EdenPhysicsDebugDecorator.h
│
└── Private/
    ├── Solver/                      # 核心求解器
    │   ├── IEdenRopeEvolution.h/cpp    # 求解器接口
    │   ├── XPBDEdenRopeEvolution.h/cpp # XPBD 求解器
    │   ├── EdenRopeSolverActor.h/cpp   # Solver Actor, 包装 Evolution
    │   └── EdenRopeWorldSubsystem.h/cpp
    │
    ├── Constraints/                 # 约束实现
    │   └── XPBD*                      # XPBD 约束 (Distance, Bend, Pin, Collision 等)
    │
    ├── Render/                      # 渲染
    │   └── EdenRopeSceneProxy.h/cpp   # FPrimitiveSceneProxy, 程序化管状 Mesh
    │
    ├── Components/                  # Pin 组件
    │   ├── RopePinComponent.h/cpp
    │   └── RopePinActor.h/cpp
    │
    ├── RigidRope/                   # 刚体绳索变体
    │   ├── EdenRigidRopeAsset/Component/Actor
    │
    ├── EdenRopeComponent.h/cpp      # 主 Rope Component (UMeshComponent)
    ├── EdenRopeActor.h/cpp          # Rope Actor
    ├── Handles/                     # Handle 封装
    ├── Interface/                   # IEdenRopeInterface
    ├── RigidBodyProxy/              # 物理碰撞代理
    ├── Debug/                       # Debug 可视化
    └── Macro/                       # 宏定义
```

## Architecture

```
UEdenRopeSettings (Project Settings)
    ↓ FEdenRopeSolverConfiguration
AEdenRopeSolverActor → ApplyConfig() → FXPBDEdenRopeEvolution
    ↑ Register/Unregister
UEdenRopeComponent (每根绳子)
```

- **Config 流向**: UEdenRopeSettings → SolverConfig → ApplyConfig() → Evolution public members
- **Solver** 统一管理所有 rope 的物理模拟，Component 只负责注册粒子和渲染参数

## Constraints Pattern

每个约束: header + cpp in `Constraints/`

```cpp
// 通用接口
void Project(float Dt, ...);   // 投影约束
void ResetLambda();             // 重置 Lambda 累积
```

XPBD 约束使用 Compliance (alpha) + Damping (beta) + Lambda 累积。动态约束 (collision) 每帧通过 `Generate*()` 重新生成。

各约束的详细实现文档见 `Docs/Constraints/`：

| 约束 | 文档 |
|------|------|
| Pin（粒子附着刚体/世界点） | [Docs/Constraints/PinConstraint.md](Docs/Constraints/PinConstraint.md) |

## Coordinate System (Rod/Oriented Particles)

粒子局部坐标系 (左手系)，站在杆起点面向杆方向:

```
     X (Up/Normal)
     |
     |
     +---→ Y (Left/Binormal)
    /
   Z (Forward/Tangent, 沿杆方向)
```

- **Z 轴**: Forward/Tangent，沿绳子/杆方向
- **X 轴**: Up/Normal，法线方向
- **Y 轴**: Left/Binormal，副法线方向

从 quaternion 提取: `GetAxisZ()` = Tangent, `GetAxisX()` = Up, `GetAxisY()` = Left

选择 Z 做 forward 的原因: UE 世界 Z 朝上，绳子竖直时 quaternion 接近 identity，数值稳定。

## Rendering

程序化管状 Mesh, 详见 [Docs/Rendering.md](Docs/Rendering.md)。

## Docs

| 文档 | 内容 |
|------|------|
| [Docs/PhysicsLoop.md](Docs/PhysicsLoop.md) | Simulate 步骤、Fixed/Variable Timestep、渲染插值 |
| [Docs/Rendering.md](Docs/Rendering.md) | 渲染管线与 Mesh 生成（SceneProxy、切线帧、性能） |
| [Docs/Constraints/PinConstraint.md](Docs/Constraints/PinConstraint.md) | Pin 约束完整实现（12 步流程、数据流、注意事项） |
