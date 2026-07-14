# Rendering

## Overview

EdenRope 支持两种渲染模式，通过 `UEdenRopeComponent.RenderMode` 切换：

| 模式 | 描述 | 底层机制 |
|------|------|---------|
| **Tube** (默认) | 程序化管状圆柱 Mesh | `FPrimitiveSceneProxy` + 自定义顶点 Buffer |
| **Segment Mesh** | 每段 rope 放置一个 Static Mesh 实例 | `UInstancedStaticMeshComponent` (ISMC) |

Tube 模式支持两种 tangent frame 计算方式：
- **Parallel Transport** (默认): 从粒子位置自动计算，最小扭转
- **Oriented Particles**: 从 per-particle quaternion 提取 tangent frame

---

## Tube 模式

### Pipeline

```text
UEdenRopeComponent (Game Thread)
    │  SendRenderDynamicData_Concurrent()
    │  世界坐标 → Component 局部坐标
    │  (oriented 时同时转换 quaternion)
    ↓
FEdenRopeRenderDynamicData { RopePoints, RopeOrientations (optional) }
    │  ENQUEUE_RENDER_COMMAND
    ↓
FEdenRopeSceneProxy::SetDynamicData_RenderThread()
    │  BuildRopeMesh() → CachedVertices + CachedIndices (预分配复用)
    │  每帧上传: Position + Tangent buffers
    │  首帧上传: Color + TexCoord + Index buffers
    ↓
GetDynamicMeshElements() / DrawStaticElements()
    │  FMeshBatch → PT_TriangleList
    ↓
GPU Draw
```

### Tangent Frame Modes

#### Parallel Transport (默认, `bUseOrientedParticles = false`)

无 quaternion 输入时自动计算:

1. 中心差分计算 tangent: `T[i] = normalize(P[i+1] - P[i-1])`
2. 初始化首个 frame: Gram-Schmidt 正交化选初始 normal
3. 沿绳子传播: `RotQ = FindBetweenNormals(T[i-1], T[i])`, 旋转 Up/Left

优势: 相邻粒子 frame 连续，无扭转突变。

#### Oriented Particles (`bUseOrientedParticles = true`)

从 per-particle quaternion 直接提取 tangent frame:

```cpp
TangentDir = Orientation.GetAxisZ();   // Z = Forward (沿绳索方向)
UpDir      = Orientation.GetAxisX();   // X = Up/Normal
LeftDir    = Orientation.GetAxisY();   // Y = Left/Binormal
```

轴向约定详见 [CLAUDE.md](../CLAUDE.md) 中 Coordinate System 章节。

### Mesh Generation (`BuildRopeMesh`)

#### Topology

```text
Vertices:  (SegmentNum + 1) × (SideNum + 1)
Triangles: SegmentNum × SideNum × 2
```

- `SegmentNum` = 粒子数 - 1
- `SideNum` = 可配置 (1~16, 默认 4)

#### Vertex Generation

对每个粒子位置，生成一圈顶点:

1. 计算 tangent frame (Parallel Transport 或 Quaternion，见上)
2. 沿圆周均匀采样: `OutDir = cos(angle) * Up - sin(angle) * Left`
3. 每个顶点偏移 `OutDir × 0.5 × RopeWidth`

#### Vertex Data

```text
Position:  Component 局部空间 (cm)
TangentX:  Tangent (沿绳索方向, Z 轴)
TangentZ:  Normal (径向向外, OutDir)
TangentY:  Cross(OutDir, Tangent)
UV0:       (PointIdx × TileMaterial, AroundFrac)   // per-segment tiling: 每段覆盖一个完整 UV tile
Color:     White (255, 255, 255)
```

#### Index Generation

每个 Quad (两个相邻 ring 的相邻顶点) 分两个三角形:

```text
TL--TR        Tri1: TL, BL, TR
|  / |        Tri2: TR, BL, BR
| /  |
BL--BR
```

Index 只在首帧构建 (topology 不变)。

### GPU Buffers & Performance

所有 Buffer 均为 `BUF_Dynamic`:

| Buffer | 每帧上传 | 首帧上传 |
|--------|---------|---------|
| `PositionVertexBuffer` | Yes | Yes |
| `TangentsVertexBuffer` | Yes | Yes |
| `TexCoordVertexBuffer` | No | Yes |
| `ColorVertexBuffer` | No | Yes |
| `IndexBuffer` | No | Yes |

CPU 端 `CachedVertices` / `CachedIndices` 预分配为成员变量，不做每帧 heap alloc。通过 `FLocalVertexFactory` 绑定到 shader。

### Tube 模式参数

| Property | Default | Description |
|----------|---------|-------------|
| `CableWidth` | 5.0 cm | 绳索直径 |
| `NumSides` | 4 | 截面边数 (1~16) |
| `TileMaterial` | 1.0 | 每段的 UV tiling 系数 (1.0 = 每段一个完整 tile，2.0 = 每段两个 tile) |
| `Material` | Default | 绳索材质，为空时用引擎默认材质 |

### Coordinate Space (Tube)

- Solver 输出粒子**世界坐标** (已从 m 转换为 cm)
- `SendRenderDynamicData_Concurrent()` 中通过 `InverseTransformPosition` 转为 **Component 局部坐标**
- Oriented 时 quaternion 也转为 component 局部空间: `InvCompRotation * WorldRot`
- Scene Proxy 在局部空间生成 Mesh，由 Component 的 `LocalToWorld` 矩阵变换到世界空间

---

## Segment Mesh 模式

### 原理

每两个相邻粒子之间放置一个 Static Mesh 实例，利用 UE 原生 `UInstancedStaticMeshComponent` (ISMC) 进行 GPU instancing 绘制，无需自定义 SceneProxy。

```text
UEdenRopeComponent (Game Thread)
    │  TickComponent()
    │  CachedParticlePositions (世界坐标, cm)
    │  UpdateSegmentMeshInstances()
    │  每段: Mid + Rot + Scale → FTransform (世界空间)
    ↓
UInstancedStaticMeshComponent::BatchUpdateInstancesTransforms()
    │  bWorldSpace=true, bMarkRenderStateDirty=true
    │  引擎内部转换为 ISMC 局部坐标
    ↓
GPU Instanced Draw (UE 原生 ISMC 管线)
```

### Transform 计算

对第 `i` 段 (粒子 `P[i]` → `P[i+1]`):

```
Mid   = (P[i] + P[i+1]) * 0.5          // 实例放置位置（世界空间中点）
DirN  = normalize(P[i+1] - P[i])       // 段方向单位向量
Rot   = FindBetweenNormals(ForwardAxis, DirN)  // 将网格前向轴对齐到段方向

Scale[ForwardAxis]   = SegLen / RefLength       // 前向轴缩放，使 mesh 恰好填充段长
Scale[(Fwd+1) % 3]  = SegmentMeshCrossScale.X  // 截面第一轴
Scale[(Fwd+2) % 3]  = SegmentMeshCrossScale.Y  // 截面第二轴
```

`RefLength` 从 `SegmentMesh->GetBounds().BoxExtent[ForwardAxisIndex] * 2` 读取，即 mesh 在前向轴上的完整长度。

### ISMC 生命周期

| 事件 | 行为 |
|------|------|
| `OnRegister()` | `CreateSegmentMeshISMC()` — 创建 ISMC，预分配 N-1 个实例 |
| `OnUnregister()` | `DestroySegmentMeshISMC()` — 销毁 ISMC |
| `PostEditChangeProperty()` | 销毁并重建 ISMC（响应 Editor 属性变更）|
| `TickComponent()` 每帧 | `UpdateSegmentMeshInstances()` — BatchUpdate 所有实例 Transform |

ISMC 以 `RF_Transient` 标志创建，不序列化到磁盘，每次注册时重建。

### Segment Mesh 模式参数

| Property | Default | Description |
|----------|---------|-------------|
| `SegmentMesh` | None | 每段使用的 Static Mesh |
| `SegmentMeshForwardAxis` | `+X` | Mesh 自身哪条轴对齐段方向 |
| `SegmentMeshCrossScale` | (1, 1) | 截面两轴的缩放倍数 |

> **注意**: 前向轴缩放由引擎自动计算 (`SegLen / RefLength`)，`SegmentMeshCrossScale` 只控制另外两轴。

### 与 Tube 模式的关系

两条渲染路径**完全独立**，互不影响：

- Segment Mesh 模式下 `CreateSceneProxy()` 返回 `nullptr`，`SendRenderDynamicData_Concurrent()` 直接返回 — 无 tube 绘制开销
- Tube 模式下 ISMC 不存在 — 无 instancing 开销
- 在 Editor 中切换 `RenderMode` 会触发 `PostEditChangeProperty`，旧渲染资源立即销毁，新资源重建

---

## Key Files

| File | Description |
|------|-------------|
| `Private/Render/EdenRopeSceneProxy.h/.cpp` | Tube 模式: Scene Proxy + Mesh 生成 |
| `Private/EdenRopeComponent.h/.cpp` | 渲染模式选择、ISMC 管理、参数暴露 |
| `Private/Interface/EdenRopeInterface.h` | `IEdenRopeInterface`，Tube 模式渲染参数接口 |
