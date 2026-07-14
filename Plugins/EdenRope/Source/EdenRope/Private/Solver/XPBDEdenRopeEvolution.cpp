// Copyright Eden Games. All Rights Reserved.

#include "XPBDEdenRopeEvolution.h"
#include "DrawDebugHelpers.h"
#include "Macro/EdenRopeMacroDefine.h"
#include "EdenRopeBVHQuery.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Utilities.h"
#include "Chaos/Convex.h"
#include "Chaos/HeightField.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectType.h"
#include "Chaos/GJK.h"
#include "Chaos/GJKShape.h"
#include "Chaos/Collision/CapsuleConvexContactPoint.h"

DEFINE_LOG_CATEGORY_STATIC(LogXPBDEvolution, Log, All);

FXPBDEdenRopeEvolution::FXPBDEdenRopeEvolution()
{
}

// UE_DISABLE_OPTIMIZATION
void FXPBDEdenRopeEvolution::Simulate(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FXPBDEdenRopeEvolution::Simulate)
	if (ParticlePositions.Num() == 0)
	{
		return;
	}

	// Solver Units
	// Distance: m
	// Time: s
	// Mass: kg
	// Force: kg*m/s^2

	// 0. 快照当前位置和段方向，用于渲染插值（必须在任何修改之前）
	PreviousParticlePositions = ParticlePositions;
	if (SegmentOrientations.Num() > 0)
	{
		PreviousSegmentOrientations = SegmentOrientations;
	}

	// 0.5. 从 UE 拉取动态刚体状态
	UpdateDynamicRigidbodiesFromUnreal();
	
	// 1. Predict positions: Use External Force to set PredictedPositions
	// 1.1 Gravity
	// TODO: 1.2 Aerodynamic Force
	for (int32 i = 0; i < ParticlePositions.Num(); ++i)
	{
		if (ParticleInvMass[i] == 0.0f)
		{
			PredictedPositions[i] = ParticlePositions[i];
			continue;
		}

		// Update Velocity: v_pred = v + Gravity * dt
		ParticleVelocities[i] += Gravity * DeltaTime;
		
		// Predict Position: x_pred = x + v * dt
		PredictedPositions[i] = ParticlePositions[i] + ParticleVelocities[i] * DeltaTime;
	}

	// 1.5 预测段方向（角速度积分）
	if (SegmentOrientations.Num() > 0)
	{
		for (int32 k = 0; k < SegmentOrientations.Num(); ++k)
		{
			if (SegmentInvRotationalMass[k] <= 0.f)
			{
				PredictedSegmentOrientations[k] = SegmentOrientations[k];
				continue;
			}
			const FVector& W = SegmentAngularVelocities[k];
			const FQuat&   Q = SegmentOrientations[k];
			// q_pred = normalize(q + (1/2) * [w,0] * q * dt)
			FQuat dQ = FQuat(W.X, W.Y, W.Z, 0.f) * Q * (0.5f * DeltaTime);
			PredictedSegmentOrientations[k] = FQuat(
				Q.X + dQ.X, Q.Y + dQ.Y, Q.Z + dQ.Z, Q.W + dQ.W
			).GetNormalized();
		}
	}

	// 2. BroadPhase：对每根绳子 AABB 发起 BVH Overlap 查询，得到帧级候选刚体缓存（每帧一次）
	BroadPhaseCollisionDetection();

	// 3. NarrowPhase：基于 BroadPhase 候选 + 最新的 PredictedPositions 生成 Capsule 碰撞约束（每帧一次）
	NarrowPhaseCollisionDetection();

	// 3.5 生成 segment 间碰撞约束（自碰撞 + 跨绳碰撞）
	if (bAnySelfCollisionEnabled)
	{
		GenerateSegmentCollisionConstraints();
	}

	// 初始化 Lambda：所有 XPBD 约束 λ 在本帧求解开始前归零（NarrowPhase 刚重建约束，与之对齐）
	ResetLambdas();

	// 重置碰撞数据
	ResetCollisionData();

	// 重置动态刚体的速度增量
	ResetDynamicRigidbodyDeltas();

	// Constraint Solving
	for (uint32 Iter = 0; Iter < SolverIterations; ++Iter)
	{
		// 弯曲约束（根据类型选择不同的约束数组）
		if (BendConstraintType == EXPBDBendConstraintType::Isometric)
		{
			for (auto& Constraint : IsometricBendConstraints)
			{
				Constraint.Project(ParticlePositions, PredictedPositions, ParticleInvMass, DeltaTime, 0.01f);
			}
		}
		else
		{
			for (auto& Constraint : ProjectionBendConstraints)
			{
				Constraint.Project(ParticlePositions, PredictedPositions, ParticleInvMass, DeltaTime, 0.01f);
			}
		}
		
		// 距离约束
		for (auto& Constraint : DistanceConstraints)
		{
			Constraint.Project(ParticlePositions, PredictedPositions, ParticleInvMass, DeltaTime, 0.01f);
		}

		// StretchShear 约束（Rod 独有）：同时修正位置和段方向
		for (auto& Constraint : StretchShearConstraints)
		{
			Constraint.Project(
				ParticlePositions, PredictedPositions, ParticleInvMass,
				SegmentOrientations, PredictedSegmentOrientations, SegmentInvRotationalMass,
				DeltaTime);
		}

		// BendTwist 约束（Rod 独有）：仅修正段方向
		for (auto& Constraint : BendTwistConstraints)
		{
			Constraint.Project(SegmentOrientations, PredictedSegmentOrientations, SegmentInvRotationalMass, DeltaTime);
		}
		
		// Pin 约束（现在支持动态刚体，以及 Rod 旋转约束）
		for (auto& Constraint : PinConstraints)
		{
			Constraint.Project(ParticlePositions, PredictedPositions, ParticleInvMass, DynamicRigidbodies, DeltaTime, 0.01f,
				SegmentOrientations.Num() > 0 ? &PredictedSegmentOrientations : nullptr,
				SegmentOrientations.Num() > 0 ? &SegmentInvRotationalMass : nullptr);
		}

		// Segment 间碰撞约束
		for (auto& Constraint : SegmentCollisionConstraints)
		{
			Constraint.Project(PredictedPositions, ParticleInvMass);
		}

		// Capsule 碰撞约束（Per-Segment Capsule vs Chaos Geometry）
		for (auto& Constraint : CapsuleCollisionConstraints)
		{
			Constraint.Project(PredictedPositions, ParticleInvMass, DynamicRigidbodies, DeltaTime, ParticleColliderMapping, ParticleCollisionNormal);
		}
	}

	// 更新位置和速度
	for (int32 i = 0; i < ParticlePositions.Num(); ++i)
	{
		if (ParticleInvMass[i] > 0.0f)
		{
			// 更新速度：v = (x_new - x_old) / dt
			ParticleVelocities[i] = (PredictedPositions[i] - ParticlePositions[i]) / DeltaTime;

			// 更新位置
			ParticlePositions[i] = PredictedPositions[i];
		}
	}

	// 5.5 应用全局速度阻尼（指数衰减，帧率无关）
	// v *= (1 - d)^dt，与 Obi 的 UpdatePositionsJob 方式一致
	if (GlobalDamping > 0.0f)
	{
		const float VelocityScale = FMath::Pow(1.0f - FMath::Clamp(GlobalDamping, 0.0f, 1.0f), DeltaTime);
		for (int32 i = 0; i < ParticlePositions.Num(); ++i)
		{
			if (ParticleInvMass[i] > 0.0f)
			{
				ParticleVelocities[i] *= VelocityScale;
			}
		}
	}

	// 5.6 更新段角速度 + 方向（Rod only）
	if (SegmentOrientations.Num() > 0)
	{
		const float DampingScale = (GlobalDamping > 0.f)
			? FMath::Pow(1.0f - FMath::Clamp(GlobalDamping, 0.0f, 1.0f), DeltaTime)
			: 1.f;
		for (int32 k = 0; k < SegmentOrientations.Num(); ++k)
		{
			FQuat& QOld       = SegmentOrientations[k];
			const FQuat& QNew = PredictedSegmentOrientations[k];
			// 角速度：w = 2/dt * (dq * q^{-1}).xyz
			const FQuat DQ(QNew.X - QOld.X, QNew.Y - QOld.Y, QNew.Z - QOld.Z, QNew.W - QOld.W);
			const FQuat WQuat = DQ * QOld.Inverse() * (2.f / DeltaTime);
			SegmentAngularVelocities[k] = FVector(WQuat.X, WQuat.Y, WQuat.Z) * DampingScale;
			QOld = QNew;
		}
	}

	// 应用摩擦力（在速度更新之后）
	ApplyFriction();

	// 将累积的速度增量应用到 UE 刚体
	ApplyDeltasToUnrealRigidbodies();
}

FEdenRopeHandle FXPBDEdenRopeEvolution::CreateParticlesInternal(
	const FVector& StartPositionCm,
	const FVector& Direction,
	const FEdenRopeCreationParams& Params)
{
	FEdenRopeHandle Handle;

	const int32 NumParticles = Params.NumParticles;
	if (NumParticles < 2)
	{
		UE_LOG(LogXPBDEvolution, Warning, TEXT("CreateParticlesInternal: NumParticles must be at least 2"));
		return Handle;
	}

	// 单位转换：cm → m，应用 WorldScale3D
	const float LengthM         = Params.LengthCm * Params.WorldScale3D.X * 0.01f;
	const float SegmentLengthM  = LengthM / (NumParticles - 1);
	const FVector StartPosM     = StartPositionCm * 0.01f;
	const float ParticleRadiusM = Params.ParticleRadiusCm * (Params.WorldScale3D.Y + Params.WorldScale3D.Z) * 0.5f * 0.01f;

	// 记录粒子起始索引
	Handle.ParticleStartIndex = ParticlePositions.Num();
	Handle.ParticleCount      = NumParticles;

	// 创建粒子（单位：m）
	for (int32 i = 0; i < NumParticles; ++i)
	{
		const FVector PosM = StartPosM + Direction * (SegmentLengthM * i);
		ParticlePositions.Add(PosM);
		ParticleVelocities.Add(FVector::ZeroVector);
		PredictedPositions.Add(FVector::ZeroVector);

		const float InvMass = 1.0f / Params.MassPerParticle;
		ParticleInvMass.Add(InvMass);
		ParticleRadii.Add(ParticleRadiusM);
		PreviousParticlePositions.Add(PosM);

		// 初始化碰撞数据
		ParticleColliderMapping.Add(-1);
		ParticleCollisionNormal.Add(FVector::ZeroVector);
	}

	// bValid 由调用方在完成约束创建后设置
	return Handle;
}

FEdenRopeHandle FXPBDEdenRopeEvolution::CreateParticlesInternal(
	TArrayView<const FVector> PositionsCm,
	const FEdenRopeCreationParams& Params)
{
	FEdenRopeHandle Handle;

	const int32 NumParticles = PositionsCm.Num();
	if (NumParticles < 2 || NumParticles != Params.NumParticles)
	{
		UE_LOG(LogXPBDEvolution, Warning, TEXT("CreateParticlesInternal(positions): NumParticles mismatch (%d vs Params.NumParticles=%d)"),
			NumParticles, Params.NumParticles);
		return Handle;
	}

	// 应用 WorldScale3D 缩放粒子半径
	const float ParticleRadiusM = Params.ParticleRadiusCm * (Params.WorldScale3D.Y + Params.WorldScale3D.Z) * 0.5f * 0.01f;

	Handle.ParticleStartIndex = ParticlePositions.Num();
	Handle.ParticleCount      = NumParticles;

	for (int32 i = 0; i < NumParticles; ++i)
	{
		const FVector PosM = PositionsCm[i] * 0.01f;
		ParticlePositions.Add(PosM);
		ParticleVelocities.Add(FVector::ZeroVector);
		PredictedPositions.Add(FVector::ZeroVector);

		const float InvMass = 1.0f / Params.MassPerParticle;
		ParticleInvMass.Add(InvMass);
		ParticleRadii.Add(ParticleRadiusM);
		PreviousParticlePositions.Add(PosM);

		ParticleColliderMapping.Add(-1);
		ParticleCollisionNormal.Add(FVector::ZeroVector);
	}

	return Handle;
}

FEdenRopeHandle FXPBDEdenRopeEvolution::RegisterRope(
	TArrayView<const FVector> InitialPositionsCm,
	const FEdenRopeCreationParams& Params)
{
	const int32 NumParticles = Params.NumParticles;
	if (NumParticles < 2 || InitialPositionsCm.Num() != NumParticles)
	{
		UE_LOG(LogXPBDEvolution, Warning, TEXT("RegisterRope(positions): NumParticles mismatch"));
		return FEdenRopeHandle();
	}

	FEdenRopeHandle Handle = CreateParticlesInternal(InitialPositionsCm, Params);
	if (Handle.ParticleStartIndex == INDEX_NONE)
		return Handle;

	// 应用 WorldScale3D 缩放长度、半径、压缩阈值
	const float LengthM        = Params.LengthCm * Params.WorldScale3D.X * 0.01f;
	const float SegmentLengthM = LengthM / (NumParticles - 1);
	const float ParticleRadiusM = Params.ParticleRadiusCm * (Params.WorldScale3D.Y + Params.WorldScale3D.Z) * 0.5f * 0.01f;

	// Distance constraints
	Handle.DistanceConstraintStart = DistanceConstraints.Num();
	Handle.DistanceConstraintCount = NumParticles - 1;

	float DistCompliance = Params.DistanceStiffness;
	float DistCompressThresholdM = Params.DistanceCompressThresholdCm * Params.WorldScale3D.X * 0.01f;
	for (int32 i = 0; i < NumParticles - 1; ++i)
	{
		int32 GlobalIdx1 = Handle.ToGlobalIndex(i);
		int32 GlobalIdx2 = Handle.ToGlobalIndex(i + 1);
		DistanceConstraints.Add(Eden::FXPBDDistanceConstraint(this, GlobalIdx1, GlobalIdx2, SegmentLengthM, DistCompliance, DistCompressThresholdM));
	}

	// Bend constraints
	Handle.BendConstraintCount = NumParticles - 2;

	float BendCompliance = Params.BendStiffness;
	float BendThresholdM = Params.MinBendAngleCoefficient * SegmentLengthM;

	if (BendConstraintType == EXPBDBendConstraintType::Isometric)
	{
		Handle.BendConstraintStart = IsometricBendConstraints.Num();
		for (int32 i = 0; i < NumParticles - 2; ++i)
		{
			int32 GlobalIdx1 = Handle.ToGlobalIndex(i);
			int32 GlobalIdx2 = Handle.ToGlobalIndex(i + 1);
			int32 GlobalIdx3 = Handle.ToGlobalIndex(i + 2);
			IsometricBendConstraints.Add(Eden::FXPBDIsometricBendConstraint(this, GlobalIdx1, GlobalIdx2, GlobalIdx3, BendCompliance, BendThresholdM));
		}
	}
	else
	{
		Handle.BendConstraintStart = ProjectionBendConstraints.Num();
		for (int32 i = 0; i < NumParticles - 2; ++i)
		{
			int32 GlobalIdx1 = Handle.ToGlobalIndex(i);
			int32 GlobalIdx2 = Handle.ToGlobalIndex(i + 1);
			int32 GlobalIdx3 = Handle.ToGlobalIndex(i + 2);
			ProjectionBendConstraints.Add(Eden::FXPBDProjectionBendConstraint(this, GlobalIdx1, GlobalIdx2, GlobalIdx3, BendCompliance, BendThresholdM));
		}
	}

	Handle.bValid = true;
	RegisteredRopeHandles.Add(Handle);

	// Per-rope 参数
	RopeFrictionProportional.Add(Params.GroundFrictionProportional);
	RopeFrictionConstant.Add(Params.GroundFrictionConstant);
	RopeEnableSelfCollision.Add(Params.bEnableSelfCollision);
	RopeSelfCollisionSkipCount.Add(Params.SelfCollisionSkipCount);

	// 粒子 → 绳索映射
	const int32 RopeIdx = RegisteredRopeHandles.Num() - 1;
	ParticleRopeMapping.SetNum(ParticlePositions.Num());
	for (int32 i = 0; i < Handle.ParticleCount; ++i)
	{
		ParticleRopeMapping[Handle.ParticleStartIndex + i] = RopeIdx;
	}

	// 维护 bAnySelfCollisionEnabled
	if (Params.bEnableSelfCollision) { bAnySelfCollisionEnabled = true; }

	UE_LOG(LogXPBDEvolution, Log, TEXT("RegisterRope(positions): Registered rope with %d particles, %d distance constraints, %d bend constraints (Length=%.2fm)"),
		NumParticles, Handle.DistanceConstraintCount, Handle.BendConstraintCount, LengthM);

	return Handle;
}

FEdenRopeHandle FXPBDEdenRopeEvolution::RegisterRope(
	const FVector& StartPositionCm,
	const FVector& Direction,
	const FEdenRopeCreationParams& Params)
{
	const int32 NumParticles = Params.NumParticles;
	if (NumParticles < 2)
	{
		UE_LOG(LogXPBDEvolution, Warning, TEXT("RegisterRope: NumParticles must be at least 2"));
		return FEdenRopeHandle();
	}

	FEdenRopeHandle Handle = CreateParticlesInternal(StartPositionCm, Direction, Params);
	if (Handle.ParticleStartIndex == INDEX_NONE)
		return Handle;

	// 应用 WorldScale3D 缩放长度、半径、压缩阈值
	const float LengthM        = Params.LengthCm * Params.WorldScale3D.X * 0.01f;
	const float SegmentLengthM = LengthM / (NumParticles - 1);
	const float ParticleRadiusM = Params.ParticleRadiusCm * (Params.WorldScale3D.Y + Params.WorldScale3D.Z) * 0.5f * 0.01f;

	// ====== 2. 创建 Distance 约束 ======
	Handle.DistanceConstraintStart = DistanceConstraints.Num();
	Handle.DistanceConstraintCount = NumParticles - 1;

	float DistCompliance = Params.DistanceStiffness;
	float DistCompressThresholdM = Params.DistanceCompressThresholdCm * Params.WorldScale3D.X * 0.01f; // cm → m（含缩放）
	for (int32 i = 0; i < NumParticles - 1; ++i)
	{
		int32 GlobalIdx1 = Handle.ToGlobalIndex(i);
		int32 GlobalIdx2 = Handle.ToGlobalIndex(i + 1);
		DistanceConstraints.Add(Eden::FXPBDDistanceConstraint(this, GlobalIdx1, GlobalIdx2, SegmentLengthM, DistCompliance, DistCompressThresholdM));
	}

	// ====== 3. 创建 Bend 约束（根据类型创建不同的约束）======
	Handle.BendConstraintCount = NumParticles - 2;

	float BendCompliance = Params.BendStiffness;
	// 弯曲阈值 = 系数 * 段长度（单位：m）
	float BendThresholdM = Params.MinBendAngleCoefficient * SegmentLengthM;

	if (BendConstraintType == EXPBDBendConstraintType::Isometric)
	{
		Handle.BendConstraintStart = IsometricBendConstraints.Num();
		for (int32 i = 0; i < NumParticles - 2; ++i)
		{
			int32 GlobalIdx1 = Handle.ToGlobalIndex(i);
			int32 GlobalIdx2 = Handle.ToGlobalIndex(i + 1);
			int32 GlobalIdx3 = Handle.ToGlobalIndex(i + 2);
			IsometricBendConstraints.Add(Eden::FXPBDIsometricBendConstraint(this, GlobalIdx1, GlobalIdx2, GlobalIdx3, BendCompliance, BendThresholdM));
		}
	}
	else
	{
		Handle.BendConstraintStart = ProjectionBendConstraints.Num();
		for (int32 i = 0; i < NumParticles - 2; ++i)
		{
			int32 GlobalIdx1 = Handle.ToGlobalIndex(i);
			int32 GlobalIdx2 = Handle.ToGlobalIndex(i + 1);
			int32 GlobalIdx3 = Handle.ToGlobalIndex(i + 2);
			ProjectionBendConstraints.Add(Eden::FXPBDProjectionBendConstraint(this, GlobalIdx1, GlobalIdx2, GlobalIdx3, BendCompliance, BendThresholdM));
		}
	}

	Handle.bValid = true;

	// 记录 Handle 用于粒子间碰撞的相邻跳过判断
	RegisteredRopeHandles.Add(Handle);

	// Per-rope 参数
	RopeFrictionProportional.Add(Params.GroundFrictionProportional);
	RopeFrictionConstant.Add(Params.GroundFrictionConstant);
	RopeEnableSelfCollision.Add(Params.bEnableSelfCollision);
	RopeSelfCollisionSkipCount.Add(Params.SelfCollisionSkipCount);

	// 粒子 → 绳索映射
	const int32 RopeIdx = RegisteredRopeHandles.Num() - 1;
	ParticleRopeMapping.SetNum(ParticlePositions.Num());
	for (int32 i = 0; i < Handle.ParticleCount; ++i)
	{
		ParticleRopeMapping[Handle.ParticleStartIndex + i] = RopeIdx;
	}

	// 维护 bAnySelfCollisionEnabled
	if (Params.bEnableSelfCollision) { bAnySelfCollisionEnabled = true; }

	UE_LOG(LogXPBDEvolution, Log, TEXT("RegisterRope: Registered rope with %d particles, %d distance constraints, %d bend constraints (Length=%.2fm)"),
		NumParticles, Handle.DistanceConstraintCount, Handle.BendConstraintCount, LengthM);

	return Handle;
}

FEdenRopeHandle FXPBDEdenRopeEvolution::RegisterRod(
	const FVector& StartPositionCm,
	const FVector& Direction,
	const FEdenRopeCreationParams& Params)
{
	const int32 N = Params.NumParticles;
	if (N < 2)
	{
		UE_LOG(LogXPBDEvolution, Warning, TEXT("RegisterRod: NumParticles must be at least 2"));
		return FEdenRopeHandle();
	}

	// 1. 创建粒子（复用共享逻辑）
	FEdenRopeHandle Handle = CreateParticlesInternal(StartPositionCm, Direction, Params);
	if (Handle.ParticleStartIndex == INDEX_NONE)
		return Handle;

	// 应用 WorldScale3D 缩放长度
	const float LengthM      = Params.LengthCm * Params.WorldScale3D.X * 0.01f;
	const float SegLenM      = LengthM / (N - 1);
	const int32 SegStart     = SegmentOrientations.Num();

	// 2. 初始段方向（从切向计算）
	for (int32 k = 0; k < N - 1; ++k)
	{
		const FVector P0 = ParticlePositions[Handle.ParticleStartIndex + k];
		const FVector P1 = ParticlePositions[Handle.ParticleStartIndex + k + 1];
		const FVector Tangent = (P1 - P0).GetSafeNormal();
		// 找到将 Z 轴旋转到切向的四元数（材质 Z = 切向）
		const FQuat Q = FQuat::FindBetweenNormals(FVector::ZAxisVector, Tangent);
		SegmentOrientations.Add(Q);
		PredictedSegmentOrientations.Add(Q);
		PreviousSegmentOrientations.Add(Q);
		SegmentAngularVelocities.Add(FVector::ZeroVector);
		// 逆转动惯量 = InvRotMass per unit length / SegLen → per segment
	// 转动惯量 → 逆转动惯量：InvRotMass = 1.0 / (Inertia * SegLen)
		const float Inertia = Params.SegmentInertiaPerUnitLength;
		SegmentInvRotationalMass.Add(
			Inertia > 0.f
			? 1.0f / (Inertia * SegLenM)
			: 0.f);
	}

	// 3. StretchShear 约束（N-1 个）
	const int32 SSStart = StretchShearConstraints.Num();
	const FVector SSCompliance(Params.Shear1Compliance, Params.Shear2Compliance, Params.StretchCompliance);
	for (int32 k = 0; k < N - 1; ++k)
	{
		StretchShearConstraints.Add(FXPBDStretchShearConstraint(
			(uint32)(Handle.ParticleStartIndex + k),
			(uint32)(Handle.ParticleStartIndex + k + 1),
			(uint32)(SegStart + k),
			SegLenM,
			SSCompliance));
	}

	// 4. BendTwist 约束（N-2 个，rest Darboux ≈ 0 for 直绳）
	const int32 BTStart = BendTwistConstraints.Num();
	// Darboux vector components: X=Bend1 (around mat-X), Y=Bend2 (around mat-Y), Z=Twist (around mat-Z)
	const FVector BTCompliance(Params.Bend1Compliance, Params.Bend2Compliance, Params.TwistCompliance);
	for (int32 k = 0; k < N - 2; ++k)
	{
		const FQuat& Q0   = SegmentOrientations[SegStart + k];
		const FQuat& Q1   = SegmentOrientations[SegStart + k + 1];
		const FQuat  Rel  = Q0.Inverse() * Q1;
		// 直绳时 Darboux ≈ (0,0,0,0)，存为 FQuat(x,y,z,0)
		const FQuat  Rest(Rel.X, Rel.Y, Rel.Z, 0.f);
		BendTwistConstraints.Add(FXPBDBendTwistConstraint(
			(uint32)(SegStart + k),
			(uint32)(SegStart + k + 1),
			Rest,
			BTCompliance));
	}

	// 5. 填写 Rod 专属 Handle 字段
	Handle.SegmentOrientationStartIndex = SegStart;
	Handle.SegmentOrientationCount      = N - 1;
	Handle.StretchShearConstraintStart  = SSStart;
	Handle.StretchShearConstraintCount  = N - 1;
	Handle.BendTwistConstraintStart     = BTStart;
	Handle.BendTwistConstraintCount     = N - 2;
	Handle.bValid = true;

	// 记录 Handle 用于粒子间碰撞的相邻跳过判断
	RegisteredRopeHandles.Add(Handle);

	// Per-rope 参数
	RopeFrictionProportional.Add(Params.GroundFrictionProportional);
	RopeFrictionConstant.Add(Params.GroundFrictionConstant);
	RopeEnableSelfCollision.Add(Params.bEnableSelfCollision);
	RopeSelfCollisionSkipCount.Add(Params.SelfCollisionSkipCount);

	// 粒子 → 绳索映射
	const int32 RopeIdx = RegisteredRopeHandles.Num() - 1;
	ParticleRopeMapping.SetNum(ParticlePositions.Num());
	for (int32 i = 0; i < Handle.ParticleCount; ++i)
	{
		ParticleRopeMapping[Handle.ParticleStartIndex + i] = RopeIdx;
	}

	// 维护 bAnySelfCollisionEnabled
	if (Params.bEnableSelfCollision) { bAnySelfCollisionEnabled = true; }

	UE_LOG(LogXPBDEvolution, Log,
		TEXT("RegisterRod: %d particles, %d segs, %d SS, %d BT (Length=%.2fm)"),
		N, N - 1, N - 1, N - 2, LengthM);

	return Handle;
}

FEdenRopeHandle FXPBDEdenRopeEvolution::RegisterRod(
	TArrayView<const FVector> InitialPositionsCm,
	TArrayView<const FQuat> InitialOrientations,
	const FEdenRopeCreationParams& Params)
{
	const int32 N = Params.NumParticles;
	if (N < 2 || InitialPositionsCm.Num() != N || InitialOrientations.Num() != N - 1)
	{
		UE_LOG(LogXPBDEvolution, Warning, TEXT("RegisterRod(positions): NumParticles mismatch (N=%d, pos=%d, orient=%d)"),
			N, InitialPositionsCm.Num(), InitialOrientations.Num());
		return FEdenRopeHandle();
	}

	// 1. Create particles from given positions
	FEdenRopeHandle Handle = CreateParticlesInternal(InitialPositionsCm, Params);
	if (Handle.ParticleStartIndex == INDEX_NONE)
		return Handle;

	// 应用 WorldScale3D 缩放长度
	const float LengthM = Params.LengthCm * Params.WorldScale3D.X * 0.01f;
	const float SegLenM = LengthM / (N - 1);
	const int32 SegStart = SegmentOrientations.Num();

	// 2. Segment orientations from provided orientations
	for (int32 k = 0; k < N - 1; ++k)
	{
		const FQuat Q = InitialOrientations[k];
		SegmentOrientations.Add(Q);
		PredictedSegmentOrientations.Add(Q);
		PreviousSegmentOrientations.Add(Q);
		SegmentAngularVelocities.Add(FVector::ZeroVector);
		// 转动惯量 → 逆转动惯量：InvRotMass = 1.0 / (Inertia * SegLen)
		const float Inertia2 = Params.SegmentInertiaPerUnitLength;
		SegmentInvRotationalMass.Add(
			Inertia2 > 0.f
			? 1.0f / (Inertia2 * SegLenM)
			: 0.f);
	}

	// 3. StretchShear constraints (N-1)
	const int32 SSStart = StretchShearConstraints.Num();
	const FVector SSCompliance(Params.Shear1Compliance, Params.Shear2Compliance, Params.StretchCompliance);
	for (int32 k = 0; k < N - 1; ++k)
	{
		StretchShearConstraints.Add(FXPBDStretchShearConstraint(
			(uint32)(Handle.ParticleStartIndex + k),
			(uint32)(Handle.ParticleStartIndex + k + 1),
			(uint32)(SegStart + k),
			SegLenM,
			SSCompliance));
	}

	// 4. BendTwist constraints (N-2)
	const int32 BTStart = BendTwistConstraints.Num();
	const FVector BTCompliance(Params.Bend1Compliance, Params.Bend2Compliance, Params.TwistCompliance);
	for (int32 k = 0; k < N - 2; ++k)
	{
		FQuat Rest;
		if (Params.bKeepInitialShape)
		{
			// 从初始方向四元数计算 Darboux 向量，保持初始曲率/扭率
			const FQuat& Q0  = SegmentOrientations[SegStart + k];
			const FQuat& Q1  = SegmentOrientations[SegStart + k + 1];
			const FQuat  Rel = Q0.Inverse() * Q1;
			Rest = FQuat(Rel.X, Rel.Y, Rel.Z, 0.f);
		}
		else
		{
			// RestDarboux = 0，平衡态为直线
			Rest = FQuat(0.f, 0.f, 0.f, 0.f);
		}
		BendTwistConstraints.Add(FXPBDBendTwistConstraint(
			(uint32)(SegStart + k),
			(uint32)(SegStart + k + 1),
			Rest,
			BTCompliance));
	}

	// 5. Fill Rod-specific Handle fields
	Handle.SegmentOrientationStartIndex = SegStart;
	Handle.SegmentOrientationCount      = N - 1;
	Handle.StretchShearConstraintStart  = SSStart;
	Handle.StretchShearConstraintCount  = N - 1;
	Handle.BendTwistConstraintStart     = BTStart;
	Handle.BendTwistConstraintCount     = N - 2;
	Handle.bValid = true;

	RegisteredRopeHandles.Add(Handle);

	// Per-rope 参数
	RopeFrictionProportional.Add(Params.GroundFrictionProportional);
	RopeFrictionConstant.Add(Params.GroundFrictionConstant);
	RopeEnableSelfCollision.Add(Params.bEnableSelfCollision);
	RopeSelfCollisionSkipCount.Add(Params.SelfCollisionSkipCount);

	// 粒子 → 绳索映射
	const int32 RopeIdx2 = RegisteredRopeHandles.Num() - 1;
	ParticleRopeMapping.SetNum(ParticlePositions.Num());
	for (int32 i = 0; i < Handle.ParticleCount; ++i)
	{
		ParticleRopeMapping[Handle.ParticleStartIndex + i] = RopeIdx2;
	}

	// 维护 bAnySelfCollisionEnabled
	if (Params.bEnableSelfCollision) { bAnySelfCollisionEnabled = true; }

	UE_LOG(LogXPBDEvolution, Log,
		TEXT("RegisterRod(positions): %d particles, %d segs, %d SS, %d BT (Length=%.2fm)"),
		N, N - 1, N - 1, N - 2, LengthM);

	return Handle;
}

void FXPBDEdenRopeEvolution::UnregisterRope(FEdenRopeHandle& Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}

	// 只标记失效，不真正删除数据
	// 将对应粒子的 InvMass 设为 0（固定），这样它们不会参与模拟
	for (int32 i = 0; i < Handle.ParticleCount; ++i)
	{
		const int32 GlobalIndex = Handle.ToGlobalIndex(i);
		if (GlobalIndex < ParticleInvMass.Num())
		{
			ParticleInvMass[GlobalIndex] = 0.0f;
		}
		// 清理粒子 → 绳索映射
		if (GlobalIndex < ParticleRopeMapping.Num())
		{
			ParticleRopeMapping[GlobalIndex] = INDEX_NONE;
		}
	}

	Handle.bValid = false;

	// 重新计算 bAnySelfCollisionEnabled
	bAnySelfCollisionEnabled = false;
	for (int32 i = 0; i < RopeEnableSelfCollision.Num(); ++i)
	{
		if (RegisteredRopeHandles.IsValidIndex(i) && RegisteredRopeHandles[i].IsValid() && RopeEnableSelfCollision[i])
		{
			bAnySelfCollisionEnabled = true;
			break;
		}
	}

#if WITH_EDITOR
	// Debug Registry 墓碑标记
	DebugRegistry.UnregisterRope(Handle);
#endif

	UE_LOG(LogXPBDEvolution, Log, TEXT("UnregisterRope: Invalidated rope handle"));
}

FEdenPinConstraintHandle FXPBDEdenRopeEvolution::RegisterPinConstraint(
	uint32 ParticleGlobalIndex,
	const FVector& PositionCm)
{
	FEdenPinConstraintHandle Handle;
	Handle.PinnedParticleIndex = ParticleGlobalIndex;
	Handle.PinConstraintIndex = PinConstraints.Num();
	// 单位转换：cm → m
	const FVector PositionM = PositionCm * 0.01f;
	PinConstraints.Add(Eden::FXPBDPinConstraint(this, ParticleGlobalIndex, PositionM));
	Handle.bValid = true;

#if WITH_EDITOR
	{
		const int32 PIdx = (int32)ParticleGlobalIndex;
		DebugRegistry.RegisterConstraint(EEdenConstraintKind::Pin, TArrayView<const int32>(&PIdx, 1));
	}
#endif

	return Handle;
}

FEdenPinConstraintHandle FXPBDEdenRopeEvolution::RegisterPinConstraintToComponent(
	uint32 ParticleGlobalIndex,
	UPrimitiveComponent* Component,
	const FVector& LocalOffsetCm)
{
	FEdenPinConstraintHandle Handle;
	
	if (!Component)
	{
		UE_LOG(LogXPBDEvolution, Warning, TEXT("RegisterPinConstraintToComponent: Component is null"));
		return Handle;
	}

	// 注册动态刚体（如果尚未注册）
	int32 DynamicRBIndex = RegisterDynamicRigidbody(Component);
	if (DynamicRBIndex < 0)
	{
		UE_LOG(LogXPBDEvolution, Warning, TEXT("RegisterPinConstraintToComponent: Failed to register dynamic rigidbody"));
		return Handle;
	}

	Handle.PinnedParticleIndex = ParticleGlobalIndex;
	Handle.PinConstraintIndex = PinConstraints.Num();
	
	// 单位转换：cm → m
	const FVector LocalOffsetM = LocalOffsetCm * 0.01f;
	PinConstraints.Add(Eden::FXPBDPinConstraint(this, ParticleGlobalIndex, DynamicRBIndex, LocalOffsetM));
	Handle.bValid = true;
	
	UE_LOG(LogXPBDEvolution, Log, TEXT("RegisterPinConstraintToComponent: Registered pin constraint to component %s (RBIndex=%d)"), 
		*Component->GetName(), DynamicRBIndex);

#if WITH_EDITOR
	{
		const int32 PIdx = (int32)ParticleGlobalIndex;
		DebugRegistry.RegisterConstraint(EEdenConstraintKind::Pin, TArrayView<const int32>(&PIdx, 1));
	}
#endif

	return Handle;
}

void FXPBDEdenRopeEvolution::UpdatePinConstraintPosition(
	const FEdenPinConstraintHandle& Handle,
	const FVector& NewPositionCm)
{
	if (!Handle.IsValid())
	{
		return;
	}

	if (Handle.PinConstraintIndex < 0 || Handle.PinConstraintIndex >= PinConstraints.Num())
	{
		UE_LOG(LogXPBDEvolution, Warning, TEXT("UpdatePinConstraintPosition: Invalid pin constraint index %d"), Handle.PinConstraintIndex);
		return;
	}

	// 单位转换：cm → m
	PinConstraints[Handle.PinConstraintIndex].UpdateWorldPosition(NewPositionCm * 0.01f);
}

FEdenPinConstraintHandle FXPBDEdenRopeEvolution::RegisterPinConstraintWithOrientation(
	uint32 ParticleGlobalIndex,
	const FVector& PositionCm,
	int32 SegmentOrientationIndex,
	const FQuat& TargetOrientation,
	const FQuat& RestDarboux)
{
	FEdenPinConstraintHandle Handle;
	Handle.PinnedParticleIndex = ParticleGlobalIndex;
	Handle.PinConstraintIndex  = PinConstraints.Num();

	// 先创建基础位置 Pin 约束
	const FVector PositionM = PositionCm * 0.01f;
	Eden::FXPBDPinConstraint Constraint(this, ParticleGlobalIndex, PositionM);

	// 填写旋转约束字段
	Constraint.SegmentOrientationIndex = SegmentOrientationIndex;
	Constraint.TargetOrientation       = TargetOrientation;
	Constraint.RestDarboux             = RestDarboux;
	Constraint.RotationalCompliance    = 0.f; // fully constrained

	PinConstraints.Add(Constraint);
	Handle.bValid = true;

	UE_LOG(LogXPBDEvolution, Log,
		TEXT("RegisterPinConstraintWithOrientation: particle=%d, segOrient=%d"),
		ParticleGlobalIndex, SegmentOrientationIndex);

#if WITH_EDITOR
	{
		const int32 PIdx = (int32)ParticleGlobalIndex;
		const int32 SIdx = SegmentOrientationIndex;
		if (SIdx >= 0)
		{
			DebugRegistry.RegisterConstraint(EEdenConstraintKind::Pin,
				TArrayView<const int32>(&PIdx, 1),
				TArrayView<const int32>(&SIdx, 1));
		}
		else
		{
			DebugRegistry.RegisterConstraint(EEdenConstraintKind::Pin,
				TArrayView<const int32>(&PIdx, 1));
		}
	}
#endif

	return Handle;
}

void FXPBDEdenRopeEvolution::UpdatePinConstraintOrientation(
	const FEdenPinConstraintHandle& Handle,
	const FQuat& NewTargetOrientation)
{
	if (!Handle.IsValid())
	{
		return;
	}

	if (Handle.PinConstraintIndex < 0 || Handle.PinConstraintIndex >= PinConstraints.Num())
	{
		UE_LOG(LogXPBDEvolution, Warning, TEXT("UpdatePinConstraintOrientation: Invalid pin constraint index %d"), Handle.PinConstraintIndex);
		return;
	}

	PinConstraints[Handle.PinConstraintIndex].UpdateTargetOrientation(NewTargetOrientation);
}

int32 FXPBDEdenRopeEvolution::RegisterDynamicRigidbody(UPrimitiveComponent* Component)
{
	if (!Component)
	{
		return -1;
	}

	// 检查是否已经注册
	int32 ExistingIndex = FindDynamicRigidbodyIndex(Component);
	if (ExistingIndex >= 0)
	{
		return ExistingIndex;
	}

	// 创建新的代理
	int32 NewIndex = DynamicRigidbodies.Num();
	DynamicRigidbodies.Add(Eden::FDynamicRigidbodyProxy(Component));
	
	// 立即更新状态
	DynamicRigidbodies[NewIndex].UpdateFromUnreal();

	UE_LOG(LogXPBDEvolution, Log, TEXT("RegisterDynamicRigidbody: Registered component %s at index %d (Kinematic=%d)"), 
		*Component->GetName(), NewIndex, DynamicRigidbodies[NewIndex].bIsKinematic);

	return NewIndex;
}

int32 FXPBDEdenRopeEvolution::FindDynamicRigidbodyIndex(UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		return -1;
	}

	for (int32 i = 0; i < DynamicRigidbodies.Num(); ++i)
	{
		if (DynamicRigidbodies[i].OwnerComponent.Get() == Component)
		{
			return i;
		}
	}

	return -1;
}

void FXPBDEdenRopeEvolution::ResetLambdas()
{
	for (auto& Constraint : DistanceConstraints)
	{
		Constraint.ResetLambda();
	}
	for (auto& Constraint : IsometricBendConstraints)
	{
		Constraint.ResetLambda();
	}
	for (auto& Constraint : ProjectionBendConstraints)
	{
		Constraint.ResetLambda();
	}
	for (auto& Constraint : PinConstraints)
	{
		Constraint.ResetLambda();
	}
	for (auto& Constraint : SegmentCollisionConstraints)
	{
		Constraint.ResetLambda();
	}
	for (auto& Constraint : CapsuleCollisionConstraints)
	{
		Constraint.ResetLambda();
	}
	for (auto& Constraint : StretchShearConstraints)
	{
		Constraint.ResetLambda();
	}
	for (auto& Constraint : BendTwistConstraints)
	{
		Constraint.ResetLambda();
	}
}

void FXPBDEdenRopeEvolution::ResetCollisionData()
{
	for (int32 i = 0; i < ParticleColliderMapping.Num(); ++i)
	{
		ParticleColliderMapping[i] = -1;
		// Normal 不需要重置，只在 Mapping >= 0 时才使用
	}
}

void FXPBDEdenRopeEvolution::ApplyFriction()
{
	// ===== 1. Collider 碰撞摩擦（地面 + Chaos 碰撞体）=====
	for (int32 i = 0; i < ParticlePositions.Num(); ++i)
	{
		const int32 ColliderIdx = ParticleColliderMapping[i];
		if (ColliderIdx < 0) continue;  // 无碰撞
		if (ParticleInvMass[i] == 0.0f) continue;  // 固定粒子

		float k_fp, k_fc;

		if (ColliderIdx == 0)
		{
			// 地面碰撞：使用 per-rope 摩擦参数
			const int32 RopeIdx = (i < ParticleRopeMapping.Num()) ? ParticleRopeMapping[i] : INDEX_NONE;
			if (RopeIdx == INDEX_NONE || !RopeFrictionProportional.IsValidIndex(RopeIdx)) continue;
			k_fp = RopeFrictionProportional[RopeIdx];
			k_fc = RopeFrictionConstant[RopeIdx];
		}
		else
		{
			// 其他 Collider（Chaos 碰撞体）：保持现有行为
			if (ColliderIdx >= ColliderFrictionProportional.Num()) continue;
			k_fp = ColliderFrictionProportional[ColliderIdx];
			k_fc = ColliderFrictionConstant[ColliderIdx];
		}

		FVector& V = ParticleVelocities[i];
		const FVector& N = ParticleCollisionNormal[i];

		// 分解速度为法向和切向分量
		const float VdotN = FVector::DotProduct(V, N);
		const FVector V_n = VdotN * N;   // 法向分量
		FVector V_t = V - V_n;           // 切向分量

		// 1. 比例摩擦
		V_t = k_fp * V_t;

		// 2. 常值摩擦（低速停止）
		const float TangentSpeed = V_t.Size();
		if (TangentSpeed > KINDA_SMALL_NUMBER)
		{
			const float Reduction = FMath::Min(k_fc, TangentSpeed);
			V_t = V_t - Reduction * (V_t / TangentSpeed);
		}

		// 重新组合（法向分量保持不变）
		V = V_n + V_t;
	}

	// ===== 2. Segment 碰撞摩擦（自碰撞 + 跨绳碰撞）=====
	for (const auto& Constraint : SegmentCollisionConstraints)
	{
		// 获取双方绳索索引
		const int32 RopeIdxA = (Constraint.ParticleIndexA0 < ParticleRopeMapping.Num()) ? ParticleRopeMapping[Constraint.ParticleIndexA0] : INDEX_NONE;
		const int32 RopeIdxB = (Constraint.ParticleIndexB0 < ParticleRopeMapping.Num()) ? ParticleRopeMapping[Constraint.ParticleIndexB0] : INDEX_NONE;
		if (RopeIdxA == INDEX_NONE || RopeIdxB == INDEX_NONE) continue;
		if (!RopeFrictionProportional.IsValidIndex(RopeIdxA) || !RopeFrictionProportional.IsValidIndex(RopeIdxB)) continue;

		// 计算摩擦系数（同绳用自身值，跨绳用平均值）
		float k_fp, k_fc;
		if (RopeIdxA == RopeIdxB)
		{
			k_fp = RopeFrictionProportional[RopeIdxA];
			k_fc = RopeFrictionConstant[RopeIdxA];
		}
		else
		{
			k_fp = (RopeFrictionProportional[RopeIdxA] + RopeFrictionProportional[RopeIdxB]) * 0.5f;
			k_fc = (RopeFrictionConstant[RopeIdxA] + RopeFrictionConstant[RopeIdxB]) * 0.5f;
		}

		const FVector Normal = FVector(Constraint.ContactPoint.ShapeContactNormal);

		// 对 4 个端点粒子施加摩擦
		const int32 Indices[4] = { Constraint.ParticleIndexA0, Constraint.ParticleIndexA1, Constraint.ParticleIndexB0, Constraint.ParticleIndexB1 };
		for (int32 Idx : Indices)
		{
			if (ParticleInvMass[Idx] == 0.0f) continue;

			FVector& V = ParticleVelocities[Idx];
			const float VdotN = FVector::DotProduct(V, Normal);
			const FVector V_n = VdotN * Normal;
			FVector V_t = V - V_n;

			V_t = k_fp * V_t;

			const float TangentSpeed = V_t.Size();
			if (TangentSpeed > KINDA_SMALL_NUMBER)
			{
				const float Reduction = FMath::Min(k_fc, TangentSpeed);
				V_t = V_t - Reduction * (V_t / TangentSpeed);
			}

			V = V_n + V_t;
		}
	}
}

void FXPBDEdenRopeEvolution::DebugDraw(UWorld* InWorld, const FEdenRopeDebugDrawParams& Params)
{
#if WITH_EDITORONLY_DATA
	if (!InWorld)
	{
		return;
	}

	constexpr float ParticleRadius = 3.0f;
	constexpr float ConstraintRadius = 1.5f;

	// ========== 绘制粒子 ==========
	if (Params.bDrawParticle)
	{
		for (int32 i = 0; i < ParticlePositions.Num(); ++i)
		{
			// 内部单位 m → 绘制单位 cm (*100)
			const FVector PosCm = ParticlePositions[i] * 100.0f;
			DrawDebugSphere(InWorld, PosCm, ParticleRadius, 8, FColor::Red, false, -1.0f, 0, 0.5f);
		}
	}

	// ========== 绘制 Material Frame 坐标轴（仅 Rod handle）==========
	if (Params.bDrawMaterialFrameAxis)
	{
		constexpr float AxisLengthCm = 10.0f;
		constexpr float ArrowHeadSize = 3.0f;

		for (const FEdenRopeHandle& Handle : RegisteredRopeHandles)
		{
			if (!Handle.IsValid() || Handle.SegmentOrientationCount <= 0)
			{
				continue;
			}

			for (int32 s = 0; s < Handle.SegmentOrientationCount; ++s)
			{
				const int32 P0Idx = Handle.ParticleStartIndex + s;
				const int32 P1Idx = Handle.ParticleStartIndex + s + 1;
				const int32 SegIdx = Handle.SegmentOrientationStartIndex + s;

				if (!ParticlePositions.IsValidIndex(P0Idx) ||
					!ParticlePositions.IsValidIndex(P1Idx) ||
					!SegmentOrientations.IsValidIndex(SegIdx))
				{
					continue;
				}

				// 中点位置（m → cm）：(P0 + P1) * 0.5 * 100 = (P0 + P1) * 50
				const FVector MidCm = (ParticlePositions[P0Idx] + ParticlePositions[P1Idx]) * 50.0f;
				const FQuat& Q = SegmentOrientations[SegIdx];

				// X 轴（红色 = Up/Normal）
				DrawDebugDirectionalArrow(InWorld, MidCm, MidCm + Q.GetAxisX() * AxisLengthCm, ArrowHeadSize, FColor::Red, false, -1.0f, 0, 1.0f);
				// Y 轴（绿色 = Left/Binormal）
				DrawDebugDirectionalArrow(InWorld, MidCm, MidCm + Q.GetAxisY() * AxisLengthCm, ArrowHeadSize, FColor::Green, false, -1.0f, 0, 1.0f);
				// Z 轴（蓝色 = Forward/Tangent，沿杆方向）
				DrawDebugDirectionalArrow(InWorld, MidCm, MidCm + Q.GetAxisZ() * AxisLengthCm, ArrowHeadSize, FColor::Blue, false, -1.0f, 0, 1.0f);
			}
		}
	}

	// ========== 绘制 Pin 约束（洋红色：粒子→目标连线 + 目标球）==========
	if (Params.bDrawPin)
	{
		constexpr float PinParticleRadius = 4.0f;
		constexpr float PinTargetRadius = 3.0f;
		constexpr float ArrowHeadSize = 4.0f;

		for (const auto& Constraint : PinConstraints)
		{
			if (!ParticlePositions.IsValidIndex(static_cast<int32>(Constraint.ParticleIndex)))
			{
				continue;
			}

			const FVector ParticleCm = ParticlePositions[Constraint.ParticleIndex] * 100.0f;
			const FVector TargetCm = Constraint.GetTargetPosition(DynamicRigidbodies) * 100.0f;

			// 粒子处画洋红色球
			DrawDebugSphere(InWorld, ParticleCm, PinParticleRadius, 8, FColor::Magenta, false, -1.0f, 0, 0.5f);

			// 目标位置画白色球
			DrawDebugSphere(InWorld, TargetCm, PinTargetRadius, 8, FColor::White, false, -1.0f, 0, 0.5f);

			// 粒子到目标的箭头（洋红色）
			DrawDebugDirectionalArrow(InWorld, ParticleCm, TargetCm, ArrowHeadSize, FColor::Magenta, false, -1.0f, 0, 1.0f);
		}
	}

	// ========== 绘制 Distance 约束（黄色 Capsule）==========
	if (Params.bDrawDistance)
	{
		const int32 NumPositions = ParticlePositions.Num();
		for (const auto& Constraint : DistanceConstraints)
		{
			if (static_cast<int32>(Constraint.ParticleIndex1) < NumPositions &&
				static_cast<int32>(Constraint.ParticleIndex2) < NumPositions)
			{
				// 内部单位 m → 绘制单位 cm (*100)
				const FVector P1Cm = ParticlePositions[Constraint.ParticleIndex1] * 100.0f;
				const FVector P2Cm = ParticlePositions[Constraint.ParticleIndex2] * 100.0f;
				DrawConstraintCapsule(InWorld, P1Cm, P2Cm, ConstraintRadius, FColor::Yellow);
			}
		}
	}

	// ========== 绘制 Bend 约束（蓝色 Capsule）==========
	if (Params.bDrawBend)
	{
		const int32 NumPositions = ParticlePositions.Num();
		
		// 绘制 Isometric Bend 约束
		for (const auto& Constraint : IsometricBendConstraints)
		{
			// Bend 约束连接粒子1和粒子3（跨越中间粒子2）
			if (static_cast<int32>(Constraint.ParticleIndex1) < NumPositions &&
				static_cast<int32>(Constraint.ParticleIndex3) < NumPositions)
			{
				// 内部单位 m → 绘制单位 cm (*100)
				const FVector P1Cm = ParticlePositions[Constraint.ParticleIndex1] * 100.0f;
				const FVector P3Cm = ParticlePositions[Constraint.ParticleIndex3] * 100.0f;
				DrawConstraintCapsule(InWorld, P1Cm, P3Cm, ConstraintRadius, FColor::Blue);
			}
		}
		
		// 绘制 Projection Bend 约束
		for (const auto& Constraint : ProjectionBendConstraints)
		{
			// Bend 约束连接粒子1和粒子3（跨越中间粒子2）
			if (static_cast<int32>(Constraint.ParticleIndex1) < NumPositions &&
				static_cast<int32>(Constraint.ParticleIndex3) < NumPositions)
			{
				// 内部单位 m → 绘制单位 cm (*100)
				const FVector P1Cm = ParticlePositions[Constraint.ParticleIndex1] * 100.0f;
				const FVector P3Cm = ParticlePositions[Constraint.ParticleIndex3] * 100.0f;
				DrawConstraintCapsule(InWorld, P1Cm, P3Cm, ConstraintRadius, FColor::Cyan);
			}
		}
	}

	// ========== 绘制 RB Scene ==========
	// Note: RB Scene 调试绘制已移除，现在使用 BVH 碰撞检测
	// 如需调试碰撞体，请使用 Unreal Engine 的物理调试可视化

	// ========== 绘制 Per-Rope AABB（橙色 Box）==========
	if (Params.bDrawRopeAABB)
	{
		// 计算最大粒子半径（与 BroadPhaseCollisionDetection 一致）
		float MaxRadius = 0.0f;
		for (float R : ParticleRadii)
		{
			MaxRadius = FMath::Max(MaxRadius, R);
		}
		const float Margin = MaxRadius + 0.01f;

		for (const FEdenRopeHandle& Handle : RegisteredRopeHandles)
		{
			if (!Handle.IsValid() || Handle.ParticleCount < 2)
			{
				continue;
			}

			// 每根绳子整体一个 AABB（单位 m），与 per-rope BroadPhase 一致
			const Chaos::FAABB3 RopeAABB = ComputeRopeAABB(Handle, Margin);
			const FVector Min = FVector(RopeAABB.Min());
			const FVector Max = FVector(RopeAABB.Max());

			// 单位转换 m → cm
			const FVector CenterCm = (Min + Max) * 0.5f * 100.0f;
			const FVector ExtentCm = (Max - Min) * 0.5f * 100.0f;

			DrawDebugBox(InWorld, CenterCm, ExtentCm, FColor::Orange, false, -1.0f, 0, 1.0f);
		}
	}

	// ========== 绘制 Collision Constraints（绿色点 + 法线箭头 + Capsule 轮廓）==========
	if (Params.bDrawCollisionConstraints)
	{
		constexpr float ArrowLengthCm = 15.0f;
		constexpr float ArrowHeadSize = 3.0f;

		// Per-Segment Capsule 碰撞约束
		for (const auto& Constraint : CapsuleCollisionConstraints)
		{
			// Segment capsule 两端粒子位置（m → cm）
			const FVector P0Cm = ParticlePositions[Constraint.ParticleIndex0] * 100.0f;
			const FVector P1Cm = ParticlePositions[Constraint.ParticleIndex1] * 100.0f;
			const float R0 = ParticleRadii.IsValidIndex(Constraint.ParticleIndex0) ? ParticleRadii[Constraint.ParticleIndex0] : 0.0f;
			const float R1 = ParticleRadii.IsValidIndex(Constraint.ParticleIndex1) ? ParticleRadii[Constraint.ParticleIndex1] : 0.0f;
			const float CapsuleRadiusCm = FMath::Max(R0, R1) * 100.0f;

			// 绘制 segment capsule 轮廓（半透明青色）
			DrawConstraintCapsule(InWorld, P0Cm, P1Cm, CapsuleRadiusCm, FColor::Cyan);

			// 碰撞体表面点（m → cm）
			const FVector SurfacePointCm = FVector(Constraint.ContactPoint.ShapeContactPoints[1]) * 100.0f;
			const FVector SurfaceNormal = FVector(Constraint.ContactPoint.ShapeContactNormal);

			// 绘制表面点（绿色小球）
			DrawDebugSphere(InWorld, SurfacePointCm, 2.0f, 6, FColor::Green, false, -1.0f, 0, 0.5f);

			// 绘制法线箭头（绿色）
			const FVector ArrowEnd = SurfacePointCm + SurfaceNormal * ArrowLengthCm;
			DrawDebugDirectionalArrow(InWorld, SurfacePointCm, ArrowEnd, ArrowHeadSize, FColor::Green, false, -1.0f, 0, 1.0f);

			// 绘制 capsule 侧接触点（黄色小球）
			const FVector CapsuleContactCm = (P0Cm * (1.0f - Constraint.BarycentricT) + P1Cm * Constraint.BarycentricT);
			DrawDebugSphere(InWorld, CapsuleContactCm, 2.0f, 6, FColor::Yellow, false, -1.0f, 0, 0.5f);
		}
	}
#endif
}

void FXPBDEdenRopeEvolution::DrawConstraintCapsule(UWorld* InWorld, const FVector& P1, const FVector& P2, float Radius, const FColor& Color)
{
#if WITH_EDITORONLY_DATA
	const FVector Direction = P2 - P1;
	const float Length = Direction.Size();
	if (Length < KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector Center = (P1 + P2) * 0.5f;
	const float HalfHeight = Length * 0.5f;
	const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Direction.GetSafeNormal());

	DrawDebugCapsule(InWorld, Center, HalfHeight, Radius, Rotation, Color, false, -1.0f, 0, 0.5f);
#endif
}

void FXPBDEdenRopeEvolution::UpdateDynamicRigidbodiesFromUnreal()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FXPBDEdenRopeEvolution::UpdateDynamicRigidbodiesFromUnreal)
	
	for (auto& RB : DynamicRigidbodies)
	{
		RB.UpdateFromUnreal();
	}
}

void FXPBDEdenRopeEvolution::ResetDynamicRigidbodyDeltas()
{
	for (auto& RB : DynamicRigidbodies)
	{
		RB.ResetDeltas();
	}
}

void FXPBDEdenRopeEvolution::ApplyDeltasToUnrealRigidbodies()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FXPBDEdenRopeEvolution::ApplyDeltasToUnrealRigidbodies)
	
	for (auto& RB : DynamicRigidbodies)
	{
		RB.ApplyDeltasToUnreal();
	}
}

void FXPBDEdenRopeEvolution::BroadPhaseCollisionDetection()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FXPBDEdenRopeEvolution::BroadPhaseCollisionDetection)

	// 帧开始：清空上一帧的所有帧级缓存，从索引 1 重新分配
	// （索引 0 历史上属于 Ground 占位，现已移除地面，但保留起始值 1 以免影响其它逻辑）
	RopeCandidates.Reset();
	CandidateColliderMap.Reset();
	CandidateDynamicRBMap.Reset();
	NextColliderIndex = 1;

	// 早退：无 World / PhysScene / SpatialAccel / 粒子数为 0 / 未注册任何绳子
	if (!World)
	{
		return;
	}

	FPhysScene* PhysScene = World->GetPhysicsScene();
	if (!PhysScene)
	{
		return;
	}

	const Chaos::ISpatialAcceleration<Chaos::FAccelerationStructureHandle, Chaos::FReal, 3>* SpatialAccel = PhysScene->GetSpacialAcceleration();
	if (!SpatialAccel)
	{
		return;
	}

	if (PredictedPositions.Num() == 0 || RegisteredRopeHandles.Num() == 0)
	{
		return;
	}

	// 计算并缓存宽相位用到的 margin，供窄相位后续复用
	float MaxParticleRadius = 0.0f;
	for (float Radius : ParticleRadii)
	{
		MaxParticleRadius = FMath::Max(MaxParticleRadius, Radius);
	}
	CachedCollisionMargin = 0.01f; // 1cm 边距
	CachedAABBMargin = MaxParticleRadius + CachedCollisionMargin;

	// 与 RegisteredRopeHandles 按下标对齐：无效或 ParticleCount<2 的绳子保留空数组
	RopeCandidates.SetNum(RegisteredRopeHandles.Num());

	// 复用同一个 Visitor，每根绳子清空后再使用
	Eden::FEdenRopeOverlapVisitor Visitor(Eden::GEdenParticleCollisionChannel, true);

	for (int32 RopeIdx = 0; RopeIdx < RegisteredRopeHandles.Num(); ++RopeIdx)
	{
		const FEdenRopeHandle& RopeHandle = RegisteredRopeHandles[RopeIdx];
		TArray<Eden::FCollisionCandidate>& OutCandidates = RopeCandidates[RopeIdx];
		OutCandidates.Reset();

		if (!RopeHandle.IsValid() || RopeHandle.ParticleCount < 2)
		{
			continue;
		}

		// 1. 整根绳子的 AABB（内部单位：m）
		const Chaos::FAABB3 RopeAABB = ComputeRopeAABB(RopeHandle, CachedAABBMargin);

		// 转换到 Chaos 坐标系（cm）
		const Chaos::FAABB3 RopeAABBCm(RopeAABB.Min() * 100.0, RopeAABB.Max() * 100.0);

		// 2. 发起一次 BVH Overlap 查询
		Visitor.Reset();
		SpatialAccel->Overlap(RopeAABBCm, Visitor);

		// 3. 按 Candidate.Particle 指针对本绳候选结果去重（空 Geometry 一并过滤）
		TSet<Chaos::FGeometryParticle*> VisitedCandidates;
		VisitedCandidates.Reserve(Visitor.Candidates.Num());
		OutCandidates.Reserve(Visitor.Candidates.Num());
		for (const Eden::FCollisionCandidate& Candidate : Visitor.Candidates)
		{
			if (!Candidate.Geometry || !Candidate.Particle)
			{
				continue;
			}
			bool bAlreadyInSet = false;
			VisitedCandidates.Add(Candidate.Particle, &bAlreadyInSet);
			if (bAlreadyInSet)
			{
				continue;
			}
			// 按值拷贝到帧级缓存，确保 NarrowPhase 跨迭代复用时 Visitor.Candidates 清理不会影响已保存的候选
			OutCandidates.Add(Candidate);
		}
	}
}

void FXPBDEdenRopeEvolution::NarrowPhaseCollisionDetection()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FXPBDEdenRopeEvolution::NarrowPhaseCollisionDetection)

	// 每次迭代都重建碰撞约束
	CapsuleCollisionConstraints.Reset();

	if (PredictedPositions.Num() == 0 || RopeCandidates.Num() == 0)
	{
		return;
	}

	// NOTE：CandidateColliderMap / CandidateDynamicRBMap / NextColliderIndex / ColliderFrictionProportional / ColliderFrictionConstant
	// 在 BroadPhase 中已被清空。本函数在同一帧内的多次迭代里对这些 Map
	// 按需 Find 复用或 Add 新增；只要同一个 FGeometryParticle* 首次被分配 index 后，
	// 后续迭代统统 Find 复用同一个 index，从而保证同一帧内 index 的稳定性。

	for (int32 RopeIdx = 0; RopeIdx < RegisteredRopeHandles.Num(); ++RopeIdx)
	{
		const FEdenRopeHandle& RopeHandle = RegisteredRopeHandles[RopeIdx];
		if (!RopeCandidates.IsValidIndex(RopeIdx))
		{
			continue;
		}
		const TArray<Eden::FCollisionCandidate>& RopeCandidateList = RopeCandidates[RopeIdx];
		if (RopeCandidateList.Num() == 0 || !RopeHandle.IsValid() || RopeHandle.ParticleCount < 2)
		{
			continue;
		}

		// 窄相位外层：遍历本绳所有 segment
		for (int32 SegIdx = 0; SegIdx < RopeHandle.ParticleCount - 1; ++SegIdx)
		{
			const int32 Idx0 = RopeHandle.ToGlobalIndex(SegIdx);
			const int32 Idx1 = RopeHandle.ToGlobalIndex(SegIdx + 1);

			// 跳过两端都固定的 segment
			if (ParticleInvMass[Idx0] <= 0.0f && ParticleInvMass[Idx1] <= 0.0f)
			{
				continue;
			}

			// 预计算 segment capsule（cm 坐标系）
			const FVector P0Cm = PredictedPositions[Idx0] * 100.0f;
			const FVector P1Cm = PredictedPositions[Idx1] * 100.0f;
			const float R0 = ParticleRadii.IsValidIndex(Idx0) ? ParticleRadii[Idx0] : 0.0f;
			const float R1 = ParticleRadii.IsValidIndex(Idx1) ? ParticleRadii[Idx1] : 0.0f;
			const float CapsuleRadiusCm = FMath::Max(R0, R1) * 100.0f;
			const float CullDistanceCm = CapsuleRadiusCm + CachedCollisionMargin * 100.0f;

			// 构造 Chaos::FCapsule（世界坐标 cm）
			const Chaos::FCapsule SegmentCapsule(Chaos::FVec3(P0Cm), Chaos::FVec3(P1Cm), CapsuleRadiusCm);
			const Chaos::FRigidTransform3 CapsuleTransform = Chaos::FRigidTransform3::Identity;

			// 窄相位内层：segment capsule vs 本绳每一个候选刚体（O(m×n)）
			for (const Eden::FCollisionCandidate& Candidate : RopeCandidateList)
			{
				if (!Candidate.Geometry || !Candidate.Particle)
				{
					continue;
				}

				// ColliderIndex：同一 Chaos 刚体在本帧内复用索引（跨 iteration / segment / rope）
				int32 CandidateColliderIndex;
				if (const int32* FoundIdx = CandidateColliderMap.Find(Candidate.Particle))
				{
					CandidateColliderIndex = *FoundIdx;
				}
				else
				{
					CandidateColliderIndex = NextColliderIndex++;
					CandidateColliderMap.Add(Candidate.Particle, CandidateColliderIndex);

					// 确保摩擦参数数组足够大
					while (ColliderFrictionProportional.Num() <= CandidateColliderIndex)
					{
						ColliderFrictionProportional.Add(0.95f);
						ColliderFrictionConstant.Add(0.01f);
					}
				}

				// DynamicRBIndex：同一 Chaos 刚体在本帧内复用索引（跨 iteration / segment / rope）
				int32 DynamicRBIndex = -1;
				if ((Candidate.IsDynamic() || Candidate.IsKinematic()) && Candidate.OwnerComponent.IsValid())
				{
					if (const int32* FoundRBIdx = CandidateDynamicRBMap.Find(Candidate.Particle))
					{
						DynamicRBIndex = *FoundRBIdx;
					}
					else
					{
						DynamicRBIndex = RegisterDynamicRigidbody(Candidate.OwnerComponent.Get());
						CandidateDynamicRBMap.Add(Candidate.Particle, DynamicRBIndex);
					}
				}

				// 对整个 segment capsule 进行 Capsule vs Geometry 碰撞检测
				const Chaos::FRigidTransform3 GeomTransform(Candidate.WorldTransform);
				Chaos::FContactPoint ContactPointCm;
				if (DetectCapsuleCollisionWithChaosGeometry(
					SegmentCapsule, CapsuleTransform,
					*Candidate.Geometry, GeomTransform,
					CullDistanceCm, ContactPointCm))
				{
					// 将 contact point 从 cm 转换为 m
					Chaos::FContactPoint ContactPointM;
					ContactPointM.ShapeContactPoints[0] = ContactPointCm.ShapeContactPoints[0] * 0.01;
					ContactPointM.ShapeContactPoints[1] = ContactPointCm.ShapeContactPoints[1] * 0.01;
					ContactPointM.ShapeContactNormal = ContactPointCm.ShapeContactNormal;
					ContactPointM.Phi = ContactPointCm.Phi * 0.01;

					// 计算 contact point 在 segment 上的 barycentric 参数 t
					const FVector CapsuleContactM = FVector(ContactPointM.ShapeContactPoints[0]);
					const FVector SegP0 = PredictedPositions[Idx0];
					const FVector SegP1 = PredictedPositions[Idx1];
					const FVector SegDir = SegP1 - SegP0;
					const float SegLenSq = SegDir.SizeSquared();
					float BaryT = 0.0f;
					if (SegLenSq > SMALL_NUMBER)
					{
						BaryT = FMath::Clamp(FVector::DotProduct(CapsuleContactM - SegP0, SegDir) / SegLenSq, 0.0f, 1.0f);
					}

					// 创建 Capsule 碰撞约束
					if (DynamicRBIndex >= 0)
					{
						CapsuleCollisionConstraints.Add(Eden::FXPBDCapsuleCollisionConstraint(
							Idx0, Idx1,
							ContactPointM,
							BaryT,
							CandidateColliderIndex,
							DynamicRBIndex
						));
					}
					else
					{
						CapsuleCollisionConstraints.Add(Eden::FXPBDCapsuleCollisionConstraint(
							Idx0, Idx1,
							ContactPointM,
							BaryT,
							CandidateColliderIndex
						));
					}
				}
			}
		}
	}
}

void FXPBDEdenRopeEvolution::GenerateSegmentCollisionConstraints()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FXPBDEdenRopeEvolution::GenerateSegmentCollisionConstraints)

	SegmentCollisionConstraints.Reset();

	const int32 NumParticles = PredictedPositions.Num();
	if (NumParticles < 2)
	{
		return;
	}

	// 收集所有 segment（全局索引对 + 所属 rope handle index + 本地 segment index）
	struct FSegmentInfo
	{
		int32 Idx0;         // 第一个粒子全局索引
		int32 Idx1;         // 第二个粒子全局索引
		int32 RopeIndex;    // 所属绳索在 RegisteredRopeHandles 中的索引
		int32 LocalSegIdx;  // 在该绳索内的 segment 索引
	};

	TArray<FSegmentInfo> AllSegments;

	for (int32 RopeIdx = 0; RopeIdx < RegisteredRopeHandles.Num(); ++RopeIdx)
	{
		const FEdenRopeHandle& Handle = RegisteredRopeHandles[RopeIdx];
		if (!Handle.IsValid() || Handle.ParticleCount < 2)
		{
			continue;
		}

		const int32 Start = Handle.ParticleStartIndex;
		const int32 SegCount = Handle.ParticleCount - 1;

		for (int32 LocalSeg = 0; LocalSeg < SegCount; ++LocalSeg)
		{
			AllSegments.Add({Start + LocalSeg, Start + LocalSeg + 1, RopeIdx, LocalSeg});
		}
	}

	const int32 NumSegments = AllSegments.Num();
	if (NumSegments < 2)
	{
		return;
	}

	// 计算每个 segment 的 AABB（用 PredictedPositions + MaxRadius + Margin）
	// Margin 使用两端粒子的最大半径 + SelfCollisionMargin
	TArray<Chaos::FAABB3> SegAABBs;
	SegAABBs.SetNum(NumSegments);

	for (int32 i = 0; i < NumSegments; ++i)
	{
		const FSegmentInfo& Seg = AllSegments[i];
		const float MaxRadius = FMath::Max(ParticleRadii[Seg.Idx0], ParticleRadii[Seg.Idx1]);
		SegAABBs[i] = ComputeSegmentAABB(Seg.Idx0, Seg.Idx1, MaxRadius + SelfCollisionMargin);
	}

	// O(N²) segment 对检测（N = 总 segment 数，通常 < 100）
	for (int32 i = 0; i < NumSegments; ++i)
	{
		const FSegmentInfo& SegA = AllSegments[i];

		for (int32 j = i + 1; j < NumSegments; ++j)
		{
			const FSegmentInfo& SegB = AllSegments[j];

			if (SegA.RopeIndex == SegB.RopeIndex)
			{
				// 同一根绳：仅当该绳启用自碰撞时才检测
				if (!RopeEnableSelfCollision.IsValidIndex(SegA.RopeIndex) || !RopeEnableSelfCollision[SegA.RopeIndex])
				{
					continue;
				}
				// 相邻 segment 跳过（使用该绳的 per-rope SkipCount）
				const int32 SkipCount = RopeSelfCollisionSkipCount.IsValidIndex(SegA.RopeIndex) ? RopeSelfCollisionSkipCount[SegA.RopeIndex] : 3;
				if (FMath::Abs(SegA.LocalSegIdx - SegB.LocalSegIdx) <= SkipCount)
				{
					continue;
				}
			}
			else
			{
				// 不同绳：仅当任一绳启用自碰撞时才检测
				const bool bA = RopeEnableSelfCollision.IsValidIndex(SegA.RopeIndex) && RopeEnableSelfCollision[SegA.RopeIndex];
				const bool bB = RopeEnableSelfCollision.IsValidIndex(SegB.RopeIndex) && RopeEnableSelfCollision[SegB.RopeIndex];
				if (!bA && !bB)
				{
					continue;
				}
			}

			// AABB 宽相位
			if (!SegAABBs[i].Intersects(SegAABBs[j]))
			{
				continue;
			}

			// 窄相位：NearestPointsOnLineSegments
			const FVector& A0 = PredictedPositions[SegA.Idx0];
			const FVector& A1 = PredictedPositions[SegA.Idx1];
			const FVector& B0 = PredictedPositions[SegB.Idx0];
			const FVector& B1 = PredictedPositions[SegB.Idx1];

			Chaos::FReal S, T;
			Chaos::FVec3 C1, C2;
			Chaos::Utilities::NearestPointsOnLineSegments(
				Chaos::FVec3(A0), Chaos::FVec3(A1),
				Chaos::FVec3(B0), Chaos::FVec3(B1),
				S, T, C1, C2);

			const float SF = static_cast<float>(S);
			const float TF = static_cast<float>(T);
			const float RadiusA = FMath::Lerp(ParticleRadii[SegA.Idx0], ParticleRadii[SegA.Idx1], SF);
			const float RadiusB = FMath::Lerp(ParticleRadii[SegB.Idx0], ParticleRadii[SegB.Idx1], TF);

			const float Dist = static_cast<float>((C1 - C2).Size());
			const float ContactDist = RadiusA + RadiusB + SelfCollisionMargin;

			if (Dist < ContactDist && Dist > SMALL_NUMBER)
			{
				// 构造 FContactPoint
				const FVector Delta = FVector(C1 - C2);
				const FVector Normal = Delta / Dist; // 从 B 指向 A

				Chaos::FContactPoint CP;
				CP.ShapeContactPoints[0] = C1;                                                // segA 侧最近点
				CP.ShapeContactPoints[1] = C2;                                                // segB 侧最近点
				CP.ShapeContactNormal = Chaos::FVec3(Normal);                                 // 法线（B→A）
				CP.Phi = static_cast<Chaos::FReal>(Dist - RadiusA - RadiusB);                 // 穿透深度（负值=穿透）

				SegmentCollisionConstraints.Add(Eden::FXPBDSegmentCollisionConstraint(
					SegA.Idx0, SegA.Idx1, SegB.Idx0, SegB.Idx1,
					CP, SF, TF, RadiusA + RadiusB));
			}
		}
	}
}

Chaos::FAABB3 FXPBDEdenRopeEvolution::ComputeRopeAABB(const FEdenRopeHandle& Handle, float Margin) const
{
	if (!Handle.IsValid() || Handle.ParticleCount == 0)
	{
		return Chaos::FAABB3::EmptyAABB();
	}
	
	Chaos::FVec3 Min(TNumericLimits<Chaos::FReal>::Max());
	Chaos::FVec3 Max(TNumericLimits<Chaos::FReal>::Lowest());
	
	for (int32 i = 0; i < Handle.ParticleCount; ++i)
	{
		const int32 GlobalIdx = Handle.ToGlobalIndex(i);
		if (!PredictedPositions.IsValidIndex(GlobalIdx))
		{
			continue;
		}
		
		const FVector& Pos = PredictedPositions[GlobalIdx];
		const Chaos::FVec3 ChaosPos(Pos.X, Pos.Y, Pos.Z);
		const Chaos::FVec3 MarginVec(Margin, Margin, Margin);
		
		const Chaos::FVec3 PosMin = ChaosPos - MarginVec;
		const Chaos::FVec3 PosMax = ChaosPos + MarginVec;
		
		Min[0] = FMath::Min(Min[0], PosMin[0]);
		Min[1] = FMath::Min(Min[1], PosMin[1]);
		Min[2] = FMath::Min(Min[2], PosMin[2]);
		
		Max[0] = FMath::Max(Max[0], PosMax[0]);
		Max[1] = FMath::Max(Max[1], PosMax[1]);
		Max[2] = FMath::Max(Max[2], PosMax[2]);
	}
	
	return Chaos::FAABB3(Min, Max);
}

Chaos::FAABB3 FXPBDEdenRopeEvolution::ComputeSegmentAABB(int32 GlobalIdx0, int32 GlobalIdx1, float Margin) const
{
	const FVector& P0 = PredictedPositions[GlobalIdx0];
	const FVector& P1 = PredictedPositions[GlobalIdx1];
	const Chaos::FVec3 Min(
		FMath::Min(P0.X, P1.X) - Margin,
		FMath::Min(P0.Y, P1.Y) - Margin,
		FMath::Min(P0.Z, P1.Z) - Margin
	);
	const Chaos::FVec3 Max(
		FMath::Max(P0.X, P1.X) + Margin,
		FMath::Max(P0.Y, P1.Y) + Margin,
		FMath::Max(P0.Z, P1.Z) + Margin
	);
	return Chaos::FAABB3(Min, Max);
}


bool FXPBDEdenRopeEvolution::DetectCapsuleCollisionWithChaosGeometry(
	const Chaos::FCapsule& InCapsule,
	const Chaos::FRigidTransform3& CapsuleTransform,
	const Chaos::FImplicitObject& Geometry,
	const Chaos::FRigidTransform3& GeometryTransform,
	Chaos::FReal CullDistance,
	Chaos::FContactPoint& OutContactPoint) const
{
	using namespace Chaos;

	const EImplicitObjectType Type = Geometry.GetType();
	const EImplicitObjectType InnerType = GetInnerType(Type);

	switch (InnerType)
	{
	case ImplicitObjectType::Sphere:
	{
		// Capsule vs Sphere: 使用 GJKPenetration 模板
		const auto& SphereGeom = static_cast<const TSphere<FReal, 3>&>(Geometry);
		const FRigidTransform3 CapsuleToGeomTransform = CapsuleTransform.GetRelativeTransform(GeometryTransform);
		FVec3 PosGeom, PosCapsuleInGeom, NormalGeom;
		FReal Penetration;
		int32 VertexIndexA, VertexIndexB;

		if (GJKPenetration<true>(MakeGJKShape(SphereGeom), MakeGJKCoreShape(InCapsule), CapsuleToGeomTransform,
			Penetration, PosGeom, PosCapsuleInGeom, NormalGeom, VertexIndexA, VertexIndexB))
		{
			const FReal Phi = -Penetration;
			if (Phi < CullDistance)
			{
				const FVec3 PosCapsule = CapsuleToGeomTransform.InverseTransformPosition(PosCapsuleInGeom);
				OutContactPoint.ShapeContactPoints[0] = CapsuleTransform.TransformPosition(PosCapsule);
				OutContactPoint.ShapeContactPoints[1] = GeometryTransform.TransformPosition(PosGeom);
				OutContactPoint.ShapeContactNormal = GeometryTransform.TransformVectorNoScale(NormalGeom);
				OutContactPoint.Phi = Phi;
				return true;
			}
		}
		return false;
	}

	case ImplicitObjectType::Box:
	{
		// Capsule vs Box: 使用 GJKPenetration 模板
		const auto& BoxGeom = static_cast<const FImplicitBox3&>(Geometry);
		const FRigidTransform3 CapsuleToGeomTransform = CapsuleTransform.GetRelativeTransform(GeometryTransform);
		FVec3 PosGeom, PosCapsuleInGeom, NormalGeom;
		FReal Penetration;
		int32 VertexIndexA, VertexIndexB;

		if (GJKPenetration<true>(MakeGJKShape(BoxGeom), MakeGJKCoreShape(InCapsule), CapsuleToGeomTransform,
			Penetration, PosGeom, PosCapsuleInGeom, NormalGeom, VertexIndexA, VertexIndexB))
		{
			const FReal Phi = -Penetration;
			if (Phi < CullDistance)
			{
				const FVec3 PosCapsule = CapsuleToGeomTransform.InverseTransformPosition(PosCapsuleInGeom);
				OutContactPoint.ShapeContactPoints[0] = CapsuleTransform.TransformPosition(PosCapsule);
				OutContactPoint.ShapeContactPoints[1] = GeometryTransform.TransformPosition(PosGeom);
				OutContactPoint.ShapeContactNormal = GeometryTransform.TransformVectorNoScale(NormalGeom);
				OutContactPoint.Phi = Phi;
				return true;
			}
		}
		return false;
	}

	case ImplicitObjectType::Capsule:
	{
		// Capsule vs Capsule: 使用 GJKPenetration 模板
		const auto& CapsuleGeom = static_cast<const FCapsule&>(Geometry);
		const FRigidTransform3 CapsuleToGeomTransform = CapsuleTransform.GetRelativeTransform(GeometryTransform);
		FVec3 PosGeom, PosCapsuleInGeom, NormalGeom;
		FReal Penetration;
		int32 VertexIndexA, VertexIndexB;

		if (GJKPenetration<true>(MakeGJKShape(CapsuleGeom), MakeGJKCoreShape(InCapsule), CapsuleToGeomTransform,
			Penetration, PosGeom, PosCapsuleInGeom, NormalGeom, VertexIndexA, VertexIndexB))
		{
			const FReal Phi = -Penetration;
			if (Phi < CullDistance)
			{
				const FVec3 PosCapsule = CapsuleToGeomTransform.InverseTransformPosition(PosCapsuleInGeom);
				OutContactPoint.ShapeContactPoints[0] = CapsuleTransform.TransformPosition(PosCapsule);
				OutContactPoint.ShapeContactPoints[1] = GeometryTransform.TransformPosition(PosGeom);
				OutContactPoint.ShapeContactNormal = GeometryTransform.TransformVectorNoScale(NormalGeom);
				OutContactPoint.Phi = Phi;
				return true;
			}
		}
		return false;
	}

	case ImplicitObjectType::Convex:
	{
		// Capsule vs Convex: 使用 CapsuleConvexContactPoint（CHAOS_API 导出）
		// 注意：该函数返回的 ShapeContactPoints 在各自的局部空间中
		FContactPoint CP = CapsuleConvexContactPoint(InCapsule, CapsuleTransform, Geometry, GeometryTransform);
		if (CP.Phi < CullDistance)
		{
			// 转换到世界空间
			OutContactPoint.ShapeContactPoints[0] = CapsuleTransform.TransformPosition(CP.ShapeContactPoints[0]);
			OutContactPoint.ShapeContactPoints[1] = GeometryTransform.TransformPosition(CP.ShapeContactPoints[1]);
			OutContactPoint.ShapeContactNormal = GeometryTransform.TransformVectorNoScale(CP.ShapeContactNormal);
			OutContactPoint.Phi = CP.Phi;
			return true;
		}
		return false;
	}

	case ImplicitObjectType::TriangleMesh:
	{
		// Capsule vs TriangleMesh: 使用 GJKContactPoint 成员函数（CHAOS_API 导出）
		// TImplicitObjectScaled/Instanced 的 GJKContactPoint 是头文件内联模板，最终转发到 FTriangleMeshImplicitObject::GJKContactPoint
		const FRigidTransform3 QueryTM = CapsuleTransform.GetRelativeTransform(GeometryTransform);

		FVec3 Location, Normal;
		FReal Penetration;
		int32 FaceIndex = INDEX_NONE;
		bool bHit = false;

		if (IsScaled(Type))
		{
			const auto& ScaledTriMesh = static_cast<const TImplicitObjectScaled<FTriangleMeshImplicitObject>&>(Geometry);
			bHit = ScaledTriMesh.GJKContactPoint(InCapsule, QueryTM, CullDistance, Location, Normal, Penetration, FaceIndex);
		}
		else if (IsInstanced(Type))
		{
			const auto& InstancedTriMesh = static_cast<const TImplicitObjectInstanced<FTriangleMeshImplicitObject>&>(Geometry);
			bHit = InstancedTriMesh.GJKContactPoint(InCapsule, QueryTM, CullDistance, Location, Normal, Penetration, FaceIndex);
		}
		else
		{
			const auto& TriMesh = static_cast<const FTriangleMeshImplicitObject&>(Geometry);
			bHit = TriMesh.GJKContactPoint(InCapsule, QueryTM, CullDistance, Location, Normal, Penetration, FaceIndex);
		}

		if (bHit && -Penetration < CullDistance)
		{
			// GJKContactPoint 返回值在 Geometry 局部空间，需要转换到世界空间
			OutContactPoint.ShapeContactPoints[0] = CapsuleTransform.GetTranslation(); // Capsule 侧（简化，后续通过 barycentric 修正）
			OutContactPoint.ShapeContactPoints[1] = GeometryTransform.TransformPosition(Location); // 几何体表面点
			OutContactPoint.ShapeContactNormal = GeometryTransform.TransformVectorNoScale(Normal).GetSafeNormal();
			OutContactPoint.Phi = -Penetration;
			return true;
		}
		return false;
	}

	case ImplicitObjectType::HeightField:
	{
		// Capsule vs HeightField: 使用 FHeightField::GJKContactPoint（CHAOS_API 导出的成员函数）
		const auto& HF = static_cast<const FHeightField&>(Geometry);
		const FRigidTransform3 QueryTM = CapsuleTransform.GetRelativeTransform(GeometryTransform);

		FVec3 Location, Normal;
		FReal Penetration;
		int32 FaceIndex = INDEX_NONE;

		if (HF.GJKContactPoint(InCapsule, QueryTM, CullDistance, Location, Normal, Penetration, FaceIndex))
		{
			if (-Penetration < CullDistance)
			{
				OutContactPoint.ShapeContactPoints[0] = CapsuleTransform.GetTranslation(); // Capsule 侧（简化）
				OutContactPoint.ShapeContactPoints[1] = GeometryTransform.TransformPosition(Location);
				OutContactPoint.ShapeContactNormal = GeometryTransform.TransformVectorNoScale(Normal).GetSafeNormal();
				OutContactPoint.Phi = -Penetration;
				return true;
			}
		}
		return false;
	}

	case ImplicitObjectType::Transformed:
	{
		// 解包 Transformed：组合内部 transform 与外部 GeometryTransform，递归调用
		const auto& TransformedObj = static_cast<const FImplicitObjectTransformed&>(Geometry);
		const FImplicitObject* InnerObj = TransformedObj.GetTransformedObject();
		if (InnerObj)
		{
			const FRigidTransform3 CombinedTransform = TransformedObj.GetTransform() * GeometryTransform;
			return DetectCapsuleCollisionWithChaosGeometry(InCapsule, CapsuleTransform, *InnerObj, CombinedTransform, CullDistance, OutContactPoint);
		}
		return false;
	}

	case ImplicitObjectType::Union:
	case ImplicitObjectType::UnionClustered:
	{
		// Union: 遍历所有子对象，取 Phi 最小（穿透最深）的碰撞结果
		const auto& UnionObj = static_cast<const FImplicitObjectUnion&>(Geometry);
		bool bHasCollision = false;
		FReal BestPhi = TNumericLimits<FReal>::Max();

		UnionObj.ForEachObject([&](const FImplicitObject& SubObj, const FRigidTransform3& SubTransform) -> bool
		{
			// 组合子对象的 transform 与 Union 的 WorldTransform
			const FRigidTransform3 CombinedTransform = SubTransform * GeometryTransform;
			
			FContactPoint SubCP;
			if (DetectCapsuleCollisionWithChaosGeometry(InCapsule, CapsuleTransform, SubObj, CombinedTransform, CullDistance, SubCP))
			{
				if (SubCP.Phi < BestPhi)
				{
					BestPhi = SubCP.Phi;
					OutContactPoint = SubCP;
					bHasCollision = true;
				}
			}
			return true; // 继续遍历
		});

		return bHasCollision;
	}

	default:
		// LevelSet 和其他不支持的类型：直接跳过
		return false;
	}
}
// UE_ENABLE_OPTIMIZATION

// ===== 运行时参数修改 =====

void FXPBDEdenRopeEvolution::SetAllParticleMass(const FEdenRopeHandle& Handle, float NewMass)
{
	if (!Handle.IsValid() || NewMass <= 0.f)
		return;

	const float NewInvMass = 1.0f / NewMass;
	const int32 Start = Handle.ParticleStartIndex;
	const int32 Count = Handle.ParticleCount;

	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Idx = Start + i;
		if (ParticleInvMass.IsValidIndex(Idx))
		{
			// 保留固定粒子（InvMass == 0）不变
			if (ParticleInvMass[Idx] > 0.f)
			{
				ParticleInvMass[Idx] = NewInvMass;
			}
		}
	}
}

void FXPBDEdenRopeEvolution::SetSingleParticleMass(const FEdenRopeHandle& Handle, int32 LocalIndex, float NewMass)
{
	if (!Handle.IsValid() || NewMass <= 0.f)
		return;

	const int32 GlobalIdx = Handle.ToGlobalIndex(LocalIndex);
	if (GlobalIdx == INDEX_NONE || !ParticleInvMass.IsValidIndex(GlobalIdx))
		return;

	if (ParticleInvMass[GlobalIdx] > 0.f)
	{
		ParticleInvMass[GlobalIdx] = 1.0f / NewMass;
	}
}

float FXPBDEdenRopeEvolution::GetSingleParticleMass(const FEdenRopeHandle& Handle, int32 LocalIndex) const
{
	if (!Handle.IsValid())
		return -1.f;

	const int32 GlobalIdx = Handle.ToGlobalIndex(LocalIndex);
	if (GlobalIdx == INDEX_NONE || !ParticleInvMass.IsValidIndex(GlobalIdx))
		return -1.f;

	const float InvMass = ParticleInvMass[GlobalIdx];
	if (InvMass <= 0.f)
		return 0.f;

	return 1.0f / InvMass;
}

void FXPBDEdenRopeEvolution::SetAllSegmentRotationalMass(const FEdenRopeHandle& Handle, float InertiaPerUnitLength, float LengthCm)
{
	if (!Handle.IsValid() || InertiaPerUnitLength <= 0.f || LengthCm <= 0.f)
		return;

	const int32 SegStart = Handle.SegmentOrientationStartIndex;
	const int32 SegCount = Handle.SegmentOrientationCount;

	if (SegStart == INDEX_NONE || SegCount <= 0)
		return;

	// 计算每段长度（m），与 RegisterRod 逻辑一致
	const float SegLenM = (LengthCm * 0.01f) / FMath::Max(1, Handle.ParticleCount - 1);
	const float NewInvRotMass = 1.0f / (InertiaPerUnitLength * SegLenM);

	for (int32 i = 0; i < SegCount; ++i)
	{
		const int32 Idx = SegStart + i;
		if (SegmentInvRotationalMass.IsValidIndex(Idx))
		{
			SegmentInvRotationalMass[Idx] = NewInvRotMass;
		}
	}
}

#if WITH_EDITOR
void FXPBDEdenRopeEvolution::EditorNotifyRopeRegistered(const FEdenRopeHandle& Handle, const FString& InDisplayName)
{
	if (!Handle.IsValid())
	{
		return;
	}

	// 段全局起点：Rod 用 SegmentOrientationStartIndex；Rope 用 ParticleStartIndex 作为占位（同 Handle 内单调）
	const bool bIsRod = (Handle.SegmentOrientationStartIndex != INDEX_NONE && Handle.SegmentOrientationCount > 0);
	const int32 SegGlobalStart = bIsRod ? Handle.SegmentOrientationStartIndex : Handle.ParticleStartIndex;

	// 1) 登记 Rope 条目（占位名），扩容粒子/段名数组
	DebugRegistry.BeginRegisterRope(Handle, SegGlobalStart);

	// 2) 写入 Component Display Name，补齐粒子/段名
	DebugRegistry.AssignDisplayName(Handle, InDisplayName);

	// 3) 扫描 Handle 范围内的静态约束，回填 Registry 与反向索引

	// Distance 约束（Rope/Rod 都有）
	if (Handle.DistanceConstraintCount > 0 && Handle.DistanceConstraintStart != INDEX_NONE)
	{
		for (int32 k = 0; k < Handle.DistanceConstraintCount; ++k)
		{
			const int32 Idx = Handle.DistanceConstraintStart + k;
			if (!DistanceConstraints.IsValidIndex(Idx)) continue;
			const int32 P1 = (int32)DistanceConstraints[Idx].ParticleIndex1;
			const int32 P2 = (int32)DistanceConstraints[Idx].ParticleIndex2;
			const int32 ParticleIdx[2] = { P1, P2 };
			DebugRegistry.RegisterConstraint(EEdenConstraintKind::Distance,
				TArrayView<const int32>(ParticleIdx, 2));
		}
	}

	// Bend 约束（IsoBend 或 ProjBend，二选一）
	if (Handle.BendConstraintCount > 0 && Handle.BendConstraintStart != INDEX_NONE)
	{
		if (BendConstraintType == EXPBDBendConstraintType::Isometric)
		{
			for (int32 k = 0; k < Handle.BendConstraintCount; ++k)
			{
				const int32 Idx = Handle.BendConstraintStart + k;
				if (!IsometricBendConstraints.IsValidIndex(Idx)) continue;
				const int32 P1 = (int32)IsometricBendConstraints[Idx].ParticleIndex1;
				const int32 P2 = (int32)IsometricBendConstraints[Idx].ParticleIndex2;
				const int32 P3 = (int32)IsometricBendConstraints[Idx].ParticleIndex3;
				const int32 ParticleIdx[3] = { P1, P2, P3 };
				DebugRegistry.RegisterConstraint(EEdenConstraintKind::IsoBend,
					TArrayView<const int32>(ParticleIdx, 3));
			}
		}
		else
		{
			for (int32 k = 0; k < Handle.BendConstraintCount; ++k)
			{
				const int32 Idx = Handle.BendConstraintStart + k;
				if (!ProjectionBendConstraints.IsValidIndex(Idx)) continue;
				const int32 P1 = (int32)ProjectionBendConstraints[Idx].ParticleIndex1;
				const int32 P2 = (int32)ProjectionBendConstraints[Idx].ParticleIndex2;
				const int32 P3 = (int32)ProjectionBendConstraints[Idx].ParticleIndex3;
				const int32 ParticleIdx[3] = { P1, P2, P3 };
				DebugRegistry.RegisterConstraint(EEdenConstraintKind::ProjBend,
					TArrayView<const int32>(ParticleIdx, 3));
			}
		}
	}

	// StretchShear 约束（仅 Rod）
	if (Handle.StretchShearConstraintCount > 0 && Handle.StretchShearConstraintStart != INDEX_NONE)
	{
		for (int32 k = 0; k < Handle.StretchShearConstraintCount; ++k)
		{
			const int32 Idx = Handle.StretchShearConstraintStart + k;
			if (!StretchShearConstraints.IsValidIndex(Idx)) continue;
			const int32 P1 = (int32)StretchShearConstraints[Idx].ParticleIndex1;
			const int32 P2 = (int32)StretchShearConstraints[Idx].ParticleIndex2;
			const int32 S  = (int32)StretchShearConstraints[Idx].SegmentIndex;
			const int32 ParticleIdx[2] = { P1, P2 };
			const int32 SegIdx[1]      = { S };
			DebugRegistry.RegisterConstraint(EEdenConstraintKind::StretchShear,
				TArrayView<const int32>(ParticleIdx, 2),
				TArrayView<const int32>(SegIdx, 1));
		}
	}

	// BendTwist 约束（仅 Rod，仅段）
	if (Handle.BendTwistConstraintCount > 0 && Handle.BendTwistConstraintStart != INDEX_NONE)
	{
		for (int32 k = 0; k < Handle.BendTwistConstraintCount; ++k)
		{
			const int32 Idx = Handle.BendTwistConstraintStart + k;
			if (!BendTwistConstraints.IsValidIndex(Idx)) continue;
			const int32 S1 = (int32)BendTwistConstraints[Idx].SegmentIndex1;
			const int32 S2 = (int32)BendTwistConstraints[Idx].SegmentIndex2;
			const int32 SegIdx[2] = { S1, S2 };
			DebugRegistry.RegisterConstraint(EEdenConstraintKind::BendTwist,
				TArrayView<const int32>(),
				TArrayView<const int32>(SegIdx, 2));
		}
	}
}
#endif // WITH_EDITOR