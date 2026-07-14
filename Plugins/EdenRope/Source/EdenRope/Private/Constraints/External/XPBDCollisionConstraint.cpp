// Copyright Eden Games. All Rights Reserved.

#include "Constraints/External/XPBDCollisionConstraint.h"

namespace Eden
{

// ============================================================================
// FXPBDCollisionConstraint
// ============================================================================

FXPBDCollisionConstraint::FXPBDCollisionConstraint(
	uint32 InParticleIndex,
	const Chaos::FContactPoint& InContactPoint,
	int32 InColliderIndex)
	: ParticleIndex(InParticleIndex)
	, ContactPoint(InContactPoint)
	, ColliderIndex(InColliderIndex)
	, DynamicRBIndex(-1)
{
}

FXPBDCollisionConstraint::FXPBDCollisionConstraint(
	uint32 InParticleIndex,
	const Chaos::FContactPoint& InContactPoint,
	int32 InColliderIndex,
	int32 InDynamicRBIndex)
	: ParticleIndex(InParticleIndex)
	, ContactPoint(InContactPoint)
	, ColliderIndex(InColliderIndex)
	, DynamicRBIndex(InDynamicRBIndex)
{
}

void FXPBDCollisionConstraint::Project(
	TArray<FVector>& PredictPositions,
	const TArray<float>& InvMass,
	TArray<FDynamicRigidbodyProxy>& DynamicRBs,
	float DeltaTime,
	TArray<int32>& OutParticleColliderMapping,
	TArray<FVector>& OutParticleCollisionNormal)
{
	// 检查粒子是否可移动
	if (InvMass[ParticleIndex] <= 0.0f)
	{
		return;
	}

	FVector& X = PredictPositions[ParticleIndex];

	// 从 FContactPoint 中读取碰撞体表面点和法线
	const FVector SurfacePoint = FVector(ContactPoint.ShapeContactPoints[1]); // 碰撞体表面点 q_c
	const FVector SurfaceNormal = FVector(ContactPoint.ShapeContactNormal);   // 表面法线 n_c

	// 计算约束值: C = (x - q_c) · n_c
	// C < 0 表示粒子在碰撞体内部
	const float C = FVector::DotProduct(X - SurfacePoint, SurfaceNormal);

	// 只有穿透时才修正（C < 0 表示在内部）
	if (C < 0.0f)
	{
		// ====== 计算有效逆质量 ======
		const float ParticleW = InvMass[ParticleIndex];
		float RBEffectiveW = 0.0f;

		if (DynamicRBIndex >= 0 && DynamicRBIndex < DynamicRBs.Num())
		{
			const FDynamicRigidbodyProxy& RB = DynamicRBs[DynamicRBIndex];
			if (!RB.bIsKinematic)
			{
				// 刚体的有效逆质量
				RBEffectiveW = RB.GetEffectiveInverseMass(SurfaceNormal, SurfacePoint);
			}
		}

		const float TotalW = ParticleW + RBEffectiveW;
		if (TotalW < SMALL_NUMBER)
		{
			return;
		}

		// ====== 计算隐式拉格朗日乘数 ======
		// λ = -C / (w_i + w_eff)
		const float Lambda = -C / TotalW;

		// ====== 应用位置校正到粒子 ======
		// Δx_i = w_i · λ · n
		X += ParticleW * Lambda * SurfaceNormal;

		// ====== 计算并累积冲量到刚体 ======
		if (DynamicRBIndex >= 0 && DynamicRBIndex < DynamicRBs.Num())
		{
			FDynamicRigidbodyProxy& RB = DynamicRBs[DynamicRBIndex];
			if (!RB.bIsKinematic)
			{
				// 冲量 J = -λ · n / Δt（反向施加给刚体，与 Obi 和 Pin 约束一致）
				const FVector Impulse = -Lambda * SurfaceNormal / DeltaTime;
				RB.AccumulateImpulse(Impulse, SurfacePoint);
			}
		}

		// 记录碰撞信息（用于摩擦计算）
		OutParticleColliderMapping[ParticleIndex] = ColliderIndex;
		OutParticleCollisionNormal[ParticleIndex] = SurfaceNormal;
	}
}
	
} // namespace Eden
