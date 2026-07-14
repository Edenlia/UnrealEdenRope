// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Handles/EdenRopeHandle.h"
#include "Handles/EdenPinConstraintHandle.h"

#if WITH_EDITOR
class FEdenRopeDebugRegistry;
#endif

class UPrimitiveComponent;

/**
 * Debug 绘制参数
 */
struct FEdenRopeDebugDrawParams
{
	bool bDrawParticle = false;
	bool bDrawMaterialFrameAxis = false;
	bool bDrawPin = false;
	bool bDrawDistance = false;
	bool bDrawBend = false;
	bool bDrawRBScene = false;
	bool bDrawRopeAABB = false;
	bool bDrawCollisionConstraints = false;
};

/**
 * 绳索物理演化器接口
 * 负责粒子数据管理、约束管理、物理模拟
 * PBD 和 XPBD 各有自己的实现
 */
class EDENROPE_API IEdenRopeEvolution
{
public:
	virtual ~IEdenRopeEvolution() = default;

	// ===== 模拟 =====
	
	/**
	 * 执行一帧的物理模拟
	 * @param DeltaTime 帧时间（秒）
	 */
	virtual void Simulate(float DeltaTime) = 0;

	// ===== 注册/注销 =====
	
	/**
	 * 注册一根绳索
	 * 内部创建粒子和约束
	 * @param StartPositionCm 起始位置（cm）
	 * @param Direction 初始方向（单位向量）
	 * @param Params 绳索创建参数
	 * @return Handle，用于后续访问和同步
	 */
	virtual FEdenRopeHandle RegisterRope(
		const FVector& StartPositionCm,
		const FVector& Direction,
		const FEdenRopeCreationParams& Params
	) = 0;

	/**
	 * 注册一根绳索（从采样点列表初始化）
	 * @param InitialPositionsCm 初始粒子位置列表（cm），数量必须 == Params.NumParticles
	 * @param Params 绳索创建参数
	 * @return Handle，用于后续访问和同步
	 */
	virtual FEdenRopeHandle RegisterRope(
		TArrayView<const FVector> InitialPositionsCm,
		const FEdenRopeCreationParams& Params
	) = 0;

	/**
	 * 注销绳索（标记为失效）
	 */
	virtual void UnregisterRope(FEdenRopeHandle& Handle) = 0;

	/**
	 * 注册一根 Rod（位置+方向 POBCR 模型）
	 * 创建粒子、段方向、StretchShear + BendTwist 约束
	 * @param StartPositionCm 起始位置（cm）
	 * @param Direction 初始方向（单位向量）
	 * @param Params 包含 Rod 参数的创建参数（StretchCompliance、BendTwist 等）
	 * @return Handle，包含 SegmentOrientationStartIndex 等 Rod 专属字段
	 */
	virtual FEdenRopeHandle RegisterRod(
		const FVector& StartPositionCm,
		const FVector& Direction,
		const FEdenRopeCreationParams& Params
	) = 0;

	/**
	 * 注册一根 Rod（从采样点列表初始化）
	 * @param InitialPositionsCm 初始粒子位置列表（cm），数量必须 == Params.NumParticles
	 * @param InitialOrientations 初始段方向列表（世界空间四元数），数量必须 == Params.NumParticles - 1
	 * @param Params 包含 Rod 参数的创建参数
	 * @return Handle，包含 SegmentOrientationStartIndex 等 Rod 专属字段
	 */
	virtual FEdenRopeHandle RegisterRod(
		TArrayView<const FVector> InitialPositionsCm,
		TArrayView<const FQuat> InitialOrientations,
		const FEdenRopeCreationParams& Params
	) = 0;

	/**
	 * 注册 Pin 约束（固定到世界空间位置）
	 * @param ParticleGlobalIndex 粒子全局索引
	 * @param PositionCm Pin 位置（cm）
	 */
	virtual FEdenPinConstraintHandle RegisterPinConstraint(
		uint32 ParticleGlobalIndex,
		const FVector& PositionCm
	) = 0;

	/**
	 * 注册 Pin 约束（连接到动态刚体）
	 * @param ParticleGlobalIndex 粒子全局索引
	 * @param Component 要连接的 PrimitiveComponent
	 * @param LocalOffsetCm 相对于组件的局部空间偏移（cm）
	 * @return Pin 约束句柄
	 */
	virtual FEdenPinConstraintHandle RegisterPinConstraintToComponent(
		uint32 ParticleGlobalIndex,
		UPrimitiveComponent* Component,
		const FVector& LocalOffsetCm
	) = 0;

	/**
	 * 注册 Pin 约束（固定到世界空间位置 + 可选旋转约束，用于 Rod）
	 * @param ParticleGlobalIndex 粒子全局索引
	 * @param PositionCm Pin 位置（cm）
	 * @param SegmentOrientationIndex 段方向全局索引（INDEX_NONE = 无旋转约束）
	 * @param TargetOrientation 目标旋转（世界空间）
	 * @param RestDarboux 静止 Darboux 向量（绑定时计算）
	 */
	virtual FEdenPinConstraintHandle RegisterPinConstraintWithOrientation(
		uint32 ParticleGlobalIndex,
		const FVector& PositionCm,
		int32 SegmentOrientationIndex,
		const FQuat& TargetOrientation,
		const FQuat& RestDarboux
	) = 0;

	/**
	 * 更新 Pin 约束位置（仅当连接到世界空间时有效）
	 * @param Handle Pin 约束句柄
	 * @param NewPositionCm 新的 Pin 位置（cm）
	 */
	virtual void UpdatePinConstraintPosition(
		const FEdenPinConstraintHandle& Handle,
		const FVector& NewPositionCm
	) = 0;

	/**
	 * 更新 Pin 约束目标旋转（仅当旋转约束启用时有效）
	 * @param Handle Pin 约束句柄
	 * @param NewTargetOrientation 新的目标旋转（世界空间）
	 */
	virtual void UpdatePinConstraintOrientation(
		const FEdenPinConstraintHandle& Handle,
		const FQuat& NewTargetOrientation
	) = 0;

	/**
	 * 注册动态刚体
	 * @param Component 要注册的 PrimitiveComponent
	 * @return 动态刚体索引（在 DynamicRigidbodies 数组中）
	 */
	virtual int32 RegisterDynamicRigidbody(UPrimitiveComponent* Component) = 0;

	// ===== 数据访问 =====
	
	/**
	 * 获取粒子位置（单位：m）
	 */
	virtual const TArray<FVector>& GetParticlePositions() const = 0;

	/**
	 * 获取粒子速度（单位：m/s）
	 */
	virtual const TArray<FVector>& GetParticleVelocities() const = 0;

	/**
	 * 获取上一物理步的粒子位置（单位：m）
	 * 用于 Fixed Timestep 模式下的渲染插值
	 */
	virtual const TArray<FVector>& GetPreviousParticlePositions() const = 0;

	/**
	 * 获取段方向（世界空间四元数）
	 * Rod handle 填充，Rope handle 的范围为空（Num() == 0 时无 Rod 注册）
	 */
	virtual const TArray<FQuat>& GetSegmentOrientations() const = 0;

	/**
	 * 获取上一物理步的段方向（用于 Fixed Timestep 渲染插值）
	 */
	virtual const TArray<FQuat>& GetPreviousSegmentOrientations() const = 0;

	// ===== 配置 =====
	
	/**
	 * 设置求解器迭代次数
	 */
	virtual void SetSolverIterations(uint32 Iterations) = 0;
	
	/**
	 * 获取求解器迭代次数
	 */
	virtual uint32 GetSolverIterations() const = 0;

	/**
	 * 设置重力（在 BeginPlay 时调用）
	 * @param Gravity 重力向量（m/s²）
	 */
	virtual void SetGravity(const FVector& Gravity) = 0;

	/**
	 * 设置 World 指针（用于 BVH 碰撞检测访问物理场景）
	 * @param InWorld World 指针
	 */
	virtual void SetWorld(UWorld* InWorld) = 0;

	// ===== 调试 =====
	
	/**
	 * 调试绘制
	 * @param World 世界指针
	 * @param Params Debug 绘制参数（由 Solver 控制）
	 */
	virtual void DebugDraw(UWorld* World, const FEdenRopeDebugDrawParams& Params) = 0;

	// ===== 运行时参数修改 =====

	/**
	 * 设置指定 Handle 中所有粒子的质量（更新逆质量）
	 * @param Handle 绳索句柄
	 * @param NewMass 新的每粒子质量（kg）
	 */
	virtual void SetAllParticleMass(const FEdenRopeHandle& Handle, float NewMass) = 0;

	/**
	 * 设置指定 Handle 中单个粒子的质量（更新逆质量）
	 * @param Handle 绳索句柄
	 * @param LocalIndex 粒子在绳索中的局部索引（0 ~ ParticleCount-1）
	 * @param NewMass 新的质量（kg），必须 > 0
	 */
	virtual void SetSingleParticleMass(const FEdenRopeHandle& Handle, int32 LocalIndex, float NewMass) = 0;

	/**
	 * 获取指定 Handle 中单个粒子的质量
	 * @param Handle 绳索句柄
	 * @param LocalIndex 粒子在绳索中的局部索引（0 ~ ParticleCount-1）
	 * @return 质量（kg），固定粒子返回 0，无效索引返回 -1
	 */
	virtual float GetSingleParticleMass(const FEdenRopeHandle& Handle, int32 LocalIndex) const = 0;

	/**
	 * 设置指定 Handle 中所有段的转动惯量（更新逆转动惯量）
	 * @param Handle 绳索句柄（必须是 Rod handle，即包含 SegmentOrientation 数据）
	 * @param InertiaPerUnitLength 每单位长度转动惯量
	 * @param LengthCm 绳索总长度（cm），用于计算每段长度
	 */
	virtual void SetAllSegmentRotationalMass(const FEdenRopeHandle& Handle, float InertiaPerUnitLength, float LengthCm) = 0;

#if WITH_EDITOR
	// ===== Editor 专用：Debug 命名注册中心 =====

	/**
	 * 获取 Debug Registry（仅 Editor 构建可用）
	 */
	virtual FEdenRopeDebugRegistry& GetDebugRegistry() = 0;

	/**
	 * 编辑器侧通知：Component 已完成 RegisterRope/RegisterRod，上报对应的 Component Display Name
	 * Evolution 据此填充粒子名、段名，并补注册 Pin 之外的所有 Handle 内约束
	 */
	virtual void EditorNotifyRopeRegistered(const FEdenRopeHandle& Handle, const FString& InDisplayName) = 0;
#endif
};
