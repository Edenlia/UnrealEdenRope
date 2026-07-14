// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IEdenRopeEvolution.h"
#include "Constraints/RopeInternal/XPBDDistanceConstraint.h"
#include "Constraints/RopeInternal/XPBDIsometricBendConstraint.h"
#include "Constraints/RopeInternal/XPBDProjectionBendConstraint.h"
#include "Constraints/External/XPBDPinConstraint.h"
#include "Constraints/External/XPBDCapsuleCollisionConstraint.h"
#include "Constraints/External/XPBDSegmentCollisionConstraint.h"
#include "Constraints/RodInternal/XPBDStretchShearConstraint.h"
#include "Constraints/RodInternal/XPBDBendTwistConstraint.h"
#include "EdenRopeBVHQuery.h"
#include "Chaos/AABB.h"
#include "Chaos/Collision/ContactPoint.h"

#if WITH_EDITOR
#include "EdenRopeDebugRegistry.h"
#endif

class UPrimitiveComponent;

/**
 * XPBD 阻尼方法
 */
enum class EXPBDDampingMethod : uint8
{
	None,      // No Damping
	Potential  // Rayleigh dissipation potential (XPBD Paper original method)
};

/**
 * XPBD 弯曲约束类型
 */
enum class EXPBDBendConstraintType : uint8
{
	Isometric,   // 基于质心的弯曲约束
	Projection   // 基于垂直投影的弯曲约束（顽皮狗风格）
};

/**
 * XPBD (Extended Position Based Dynamics) 绳索演化器
 * 使用 Compliance 参数和拉格朗日乘子实现更稳定的约束求解
 */
class EDENROPE_API FXPBDEdenRopeEvolution : public IEdenRopeEvolution
{
public:
	FXPBDEdenRopeEvolution();
	virtual ~FXPBDEdenRopeEvolution() override = default;

	// ===== IEdenRopeEvolution 接口实现 =====
	virtual void Simulate(float DeltaTime) override;

	virtual FEdenRopeHandle RegisterRope(
		const FVector& StartPositionCm,
		const FVector& Direction,
		const FEdenRopeCreationParams& Params
	) override;

	virtual FEdenRopeHandle RegisterRope(
		TArrayView<const FVector> InitialPositionsCm,
		const FEdenRopeCreationParams& Params
	) override;

	virtual void UnregisterRope(FEdenRopeHandle& Handle) override;

	virtual FEdenPinConstraintHandle RegisterPinConstraint(
		uint32 ParticleGlobalIndex,
		const FVector& PositionCm
	) override;

	virtual FEdenPinConstraintHandle RegisterPinConstraintToComponent(
		uint32 ParticleGlobalIndex,
		UPrimitiveComponent* Component,
		const FVector& LocalOffsetCm
	) override;

	virtual FEdenPinConstraintHandle RegisterPinConstraintWithOrientation(
		uint32 ParticleGlobalIndex,
		const FVector& PositionCm,
		int32 SegmentOrientationIndex,
		const FQuat& TargetOrientation,
		const FQuat& RestDarboux
	) override;

	virtual void UpdatePinConstraintPosition(
		const FEdenPinConstraintHandle& Handle,
		const FVector& NewPositionCm
	) override;

	virtual void UpdatePinConstraintOrientation(
		const FEdenPinConstraintHandle& Handle,
		const FQuat& NewTargetOrientation
	) override;

	virtual int32 RegisterDynamicRigidbody(UPrimitiveComponent* Component) override;

	virtual FEdenRopeHandle RegisterRod(
		const FVector& StartPositionCm,
		const FVector& Direction,
		const FEdenRopeCreationParams& Params
	) override;

	virtual FEdenRopeHandle RegisterRod(
		TArrayView<const FVector> InitialPositionsCm,
		TArrayView<const FQuat> InitialOrientations,
		const FEdenRopeCreationParams& Params
	) override;

	virtual const TArray<FVector>& GetParticlePositions() const override { return ParticlePositions; }
	virtual const TArray<FVector>& GetParticleVelocities() const override { return ParticleVelocities; }
	virtual const TArray<FVector>& GetPreviousParticlePositions() const override { return PreviousParticlePositions; }
	virtual const TArray<FQuat>&   GetSegmentOrientations() const override { return SegmentOrientations; }
	virtual const TArray<FQuat>&   GetPreviousSegmentOrientations() const override { return PreviousSegmentOrientations; }

	virtual void SetSolverIterations(uint32 Iterations) override { SolverIterations = Iterations; }
	virtual uint32 GetSolverIterations() const override { return SolverIterations; }

	virtual void SetGravity(const FVector& InGravity) override { Gravity = InGravity; }

	virtual void SetWorld(UWorld* InWorld) override { World = InWorld; }

	virtual void DebugDraw(UWorld* World, const FEdenRopeDebugDrawParams& Params) override;

	virtual void SetAllParticleMass(const FEdenRopeHandle& Handle, float NewMass) override;
	virtual void SetSingleParticleMass(const FEdenRopeHandle& Handle, int32 LocalIndex, float NewMass) override;
	virtual float GetSingleParticleMass(const FEdenRopeHandle& Handle, int32 LocalIndex) const override;
	virtual void SetAllSegmentRotationalMass(const FEdenRopeHandle& Handle, float InertiaPerUnitLength, float LengthCm) override;

#if WITH_EDITOR
	virtual FEdenRopeDebugRegistry& GetDebugRegistry() override { return DebugRegistry; }
	virtual void EditorNotifyRopeRegistered(const FEdenRopeHandle& Handle, const FString& InDisplayName) override;
#endif

	// ===== XPBD 专用访问（用于调试）=====
	const TArray<Eden::FXPBDDistanceConstraint>& GetDistanceConstraints() const { return DistanceConstraints; }
	const TArray<Eden::FXPBDIsometricBendConstraint>& GetIsometricBendConstraints() const { return IsometricBendConstraints; }
	const TArray<Eden::FXPBDProjectionBendConstraint>& GetProjectionBendConstraints() const { return ProjectionBendConstraints; }
	const TArray<Eden::FXPBDPinConstraint>& GetPinConstraints() const { return PinConstraints; }

	// ===== XPBD 配置（public）=====
	EXPBDDampingMethod DampingMethod = EXPBDDampingMethod::Potential;
	EXPBDBendConstraintType BendConstraintType = EXPBDBendConstraintType::Isometric;

	// ===== 全局速度阻尼（Obi 风格指数衰减）=====
	/** 全局速度阻尼 [0, 1]，每秒损失的速度百分比。0 = 无阻尼，1 = 完全停止 */
	float GlobalDamping = 0.0f;

	// ===== Segment 碰撞配置（全局） =====
	/** 碰撞检测边距（单位：m），segment 距离 < rA + rB + margin 时生成约束 */
	float SelfCollisionMargin = 0.01f;

	/** 是否有任何绳索启用了自碰撞（注册/注销时维护，用于 Simulate 中的 early-out） */
	bool bAnySelfCollisionEnabled = false;

private:
	// ===== 粒子数据（单位：m）=====
	TArray<FVector> ParticlePositions;
	TArray<FVector> ParticleVelocities;
	TArray<float> ParticleInvMass;
	TArray<float> ParticleRadii;  // 粒子半径（单位：m），用于碰撞检测
	TArray<FVector> PredictedPositions;
	TArray<FVector> PreviousParticlePositions;  // 上一物理步的位置快照（单位：m）

	// ===== 碰撞数据 =====
	TArray<int32> ParticleColliderMapping;    // 每个粒子本帧碰撞的 Collider 索引（-1 = 无碰撞, >=1 = Collider 索引，Ground 已移除）
	TArray<FVector> ParticleCollisionNormal;  // 碰撞法线（当 Mapping >= 0 时有效）

	// ===== Collider 摩擦参数（SoA）=====
	TArray<float> ColliderFrictionProportional;  // k_fp：比例摩擦系数（0~1，1 = 无摩擦）
	TArray<float> ColliderFrictionConstant;      // k_fc：常值摩擦系数（单位：m/s），用于低速停止

	// ===== XPBD 约束 =====
	TArray<Eden::FXPBDDistanceConstraint> DistanceConstraints;
	TArray<Eden::FXPBDIsometricBendConstraint> IsometricBendConstraints;
	TArray<Eden::FXPBDProjectionBendConstraint> ProjectionBendConstraints;
	TArray<Eden::FXPBDPinConstraint> PinConstraints;
	TArray<Eden::FXPBDCapsuleCollisionConstraint> CapsuleCollisionConstraints;  // 动态生成的 Capsule 碰撞约束（Per-Segment Capsule vs Chaos Geometry）
	TArray<Eden::FXPBDSegmentCollisionConstraint> SegmentCollisionConstraints;  // 动态生成的 segment 间碰撞约束

	// ===== BroadPhase 帧级缓存（每帧 BroadPhaseCollisionDetection 写一次，NarrowPhase 每迭代读多次）=====
	/** 每根绳子（与 RegisteredRopeHandles 下标对齐）→ 去重后的候选刚体列表 */
	TArray<TArray<Eden::FCollisionCandidate>> RopeCandidates;
	/** 同一帧内 FGeometryParticle* → ColliderIndex 的去重映射（同一刚体在多次迭代间保持稳定索引） */
	TMap<Chaos::FGeometryParticle*, int32> CandidateColliderMap;
	/** 同一帧内 FGeometryParticle* → DynamicRigidbodies 索引的去重映射（仅 Dynamic/Kinematic 会写入） */
	TMap<Chaos::FGeometryParticle*, int32> CandidateDynamicRBMap;
	/** 下一个可分配的 ColliderIndex（1 起始，索引 0 为历史 Ground 占位，保留以兼容剩余逻辑） */
	int32 NextColliderIndex = 1;
	/** BroadPhase 计算好的碰撞 margin（m），供 NarrowPhase 复用，避免重复扫 MaxParticleRadius */
	float CachedCollisionMargin = 0.01f;
	/** BroadPhase 计算好的 AABB margin（m）= MaxParticleRadius + CollisionMargin */
	float CachedAABBMargin = 0.0f;

	// ===== Rod 段方向数据（仅 Rod handle 填充）=====
	TArray<FQuat>   SegmentOrientations;
	TArray<FQuat>   PredictedSegmentOrientations;
	TArray<FQuat>   PreviousSegmentOrientations;
	TArray<FVector> SegmentAngularVelocities;
	TArray<float>   SegmentInvRotationalMass;

	// ===== Rod 约束 =====
	TArray<FXPBDStretchShearConstraint> StretchShearConstraints;
	TArray<FXPBDBendTwistConstraint>    BendTwistConstraints;

	// ===== 已注册绳索的 Handle（用于粒子间碰撞的相邻跳过判断）=====
	TArray<FEdenRopeHandle> RegisteredRopeHandles;

	// ===== Per-Rope 参数（与 RegisteredRopeHandles 索引对应）=====
	TArray<float> RopeFrictionProportional;     // per-rope 比例摩擦系数 k_fp
	TArray<float> RopeFrictionConstant;         // per-rope 常值摩擦系数 k_fc
	TArray<bool>  RopeEnableSelfCollision;      // per-rope 自碰撞开关
	TArray<int32> RopeSelfCollisionSkipCount;   // per-rope 相邻 segment 跳过数

	// ===== 粒子 → 绳索索引映射 =====
	TArray<int32> ParticleRopeMapping;  // per-particle → RegisteredRopeHandles 索引（INDEX_NONE = 无效）

	// ===== 配置 =====
	uint32 SolverIterations = 3;
	FVector Gravity = FVector::ZeroVector;

	// ===== World 指针（用于访问物理场景的 BVH）=====
	UWorld* World = nullptr;

	// ===== 动态刚体 =====
	TArray<Eden::FDynamicRigidbodyProxy> DynamicRigidbodies;

#if WITH_EDITOR
	// ===== Debug 命名注册中心（仅 Editor） =====
	FEdenRopeDebugRegistry DebugRegistry;
#endif

	// ===== 内部方法 =====
	/** 创建粒子（不创建约束）。返回含粒子索引的 Handle，bValid=false */
	FEdenRopeHandle CreateParticlesInternal(
		const FVector& StartPositionCm,
		const FVector& Direction,
		const FEdenRopeCreationParams& Params);

	/** 创建粒子（从给定位置列表，不创建约束）。返回含粒子索引的 Handle，bValid=false */
	FEdenRopeHandle CreateParticlesInternal(
		TArrayView<const FVector> PositionsCm,
		const FEdenRopeCreationParams& Params);

	void ResetLambdas();
	void ResetCollisionData();
	void ApplyFriction();
	void DrawConstraintCapsule(UWorld* World, const FVector& P1, const FVector& P2, float Radius, const FColor& Color);

	/**
	 * 宽相位碰撞检测：每帧调用一次（在 SolverIterations 迭代循环外）。
	 * 基于每根绳子的 AABB 向 Chaos BVH 发起 Overlap 查询，将去重后的候选刚体写入 RopeCandidates。
	 * 不执行任何窄相位几何测试。
	 */
	void BroadPhaseCollisionDetection();

	/**
	 * 窄相位碰撞检测：每次 XPBD 迭代开头调用。
	 * 复用 BroadPhase 写入的 RopeCandidates，对每根绳子执行 O(m × n) 的 segment × rigid 碰撞测试，
	 * 每次调用都会清空并重建 CapsuleCollisionConstraints。
	 * CandidateColliderMap / CandidateDynamicRBMap 由本函数按需分配并在一帧内跨迭代复用。
	 */
	void NarrowPhaseCollisionDetection();
	
	/**
	 * 计算绳子的 AABB（用于 BVH 宽相位查询）
	 * @param Handle 绳子句柄
	 * @param Margin 扩展边距（通常为最大粒子半径 + 碰撞边距）
	 * @return 绳子的 AABB（单位：m，内部坐标系）
	 */
	Chaos::FAABB3 ComputeRopeAABB(const FEdenRopeHandle& Handle, float Margin) const;

	/**
	 * 计算单个 segment (两个相邻粒子) 的 AABB
	 * @param GlobalIdx0 第一个粒子的全局索引
	 * @param GlobalIdx1 第二个粒子的全局索引
	 * @param Margin 扩展边距（通常为最大粒子半径 + 碰撞边距）
	 * @return segment 的 AABB（单位：m，内部坐标系）
	 */
	Chaos::FAABB3 ComputeSegmentAABB(int32 GlobalIdx0, int32 GlobalIdx1, float Margin) const;
	
	/**
	 * 检测 Segment Capsule 与 Chaos Geometry 的碰撞（窄相位，新版 Capsule + GJK dispatch）
	 * 根据 Geometry 的具体类型（Sphere/Box/Capsule/Convex/TriMesh/HeightField/Union/Transformed）
	 * 分别调用 Chaos 底层对应的 Capsule 碰撞函数
	 * @param InCapsule Chaos FCapsule（世界坐标，单位：cm）
	 * @param CapsuleTransform Capsule 的 world transform（Identity，因为 Capsule 已在世界坐标中）
	 * @param Geometry Chaos 几何体（FImplicitObject）
	 * @param GeometryTransform 几何体的世界变换（Chaos 坐标系，cm）
	 * @param CullDistance 碰撞裁剪距离（cm）
	 * @param OutContactPoint 输出：碰撞接触数据（Chaos 坐标系，单位：cm）
	 * @return 是否发生碰撞
	 */
	bool DetectCapsuleCollisionWithChaosGeometry(
		const Chaos::FCapsule& InCapsule,
		const Chaos::FRigidTransform3& CapsuleTransform,
		const Chaos::FImplicitObject& Geometry,
		const Chaos::FRigidTransform3& GeometryTransform,
		Chaos::FReal CullDistance,
		Chaos::FContactPoint& OutContactPoint
	) const;

	/** 生成 segment 间碰撞约束（自碰撞 + 跨绳碰撞） */
	void GenerateSegmentCollisionConstraints();

	/** 从 UE 拉取动态刚体状态（每帧 Simulate 开始时调用） */
	void UpdateDynamicRigidbodiesFromUnreal();

	/** 重置所有动态刚体的速度增量 */
	void ResetDynamicRigidbodyDeltas();

	/** 将累积的速度增量应用到 UE 刚体（每帧 Simulate 结束时调用） */
	void ApplyDeltasToUnrealRigidbodies();

	/** 查找已注册的动态刚体索引，如果不存在则返回 -1 */
	int32 FindDynamicRigidbodyIndex(UPrimitiveComponent* Component) const;
};
