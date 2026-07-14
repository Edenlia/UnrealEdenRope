// Copyright Eden Games. All Rights Reserved.

#include "Constraints/External/XPBDCapsuleCollisionConstraint.h"

namespace Eden
{

// ============================================================================
// FXPBDCapsuleCollisionConstraint
// ============================================================================

FXPBDCapsuleCollisionConstraint::FXPBDCapsuleCollisionConstraint(
	uint32 InIdx0,
	uint32 InIdx1,
	const Chaos::FContactPoint& InContactPoint,
	float InBarycentricT,
	int32 InColliderIndex)
	: ParticleIndex0(InIdx0)
	, ParticleIndex1(InIdx1)
	, ContactPoint(InContactPoint)
	, BarycentricT(InBarycentricT)
	, ColliderIndex(InColliderIndex)
	, DynamicRBIndex(-1)
{
}

FXPBDCapsuleCollisionConstraint::FXPBDCapsuleCollisionConstraint(
	uint32 InIdx0,
	uint32 InIdx1,
	const Chaos::FContactPoint& InContactPoint,
	float InBarycentricT,
	int32 InColliderIndex,
	int32 InDynamicRBIndex)
	: ParticleIndex0(InIdx0)
	, ParticleIndex1(InIdx1)
	, ContactPoint(InContactPoint)
	, BarycentricT(InBarycentricT)
	, ColliderIndex(InColliderIndex)
	, DynamicRBIndex(InDynamicRBIndex)
{
}

void FXPBDCapsuleCollisionConstraint::Project(
	TArray<FVector>& PredictedPositions,
	const TArray<float>& InvMass,
	TArray<FDynamicRigidbodyProxy>& DynamicRBs,
	float DeltaTime,
	TArray<int32>& OutParticleColliderMapping,
	TArray<FVector>& OutParticleCollisionNormal)
{
	const float W0 = InvMass[ParticleIndex0];
	const float W1 = InvMass[ParticleIndex1];

	// 两个端点都固定则跳过
	if (W0 <= 0.0f && W1 <= 0.0f)
	{
		return;
	}

	const FVector& X0 = PredictedPositions[ParticleIndex0];
	const FVector& X1 = PredictedPositions[ParticleIndex1];

	// 从 ContactPoint 中读取碰撞体表面点和法线
	const FVector SurfacePoint = FVector(ContactPoint.ShapeContactPoints[1]); // 碰撞体表面点 q_c（m）
	const FVector SurfaceNormal = FVector(ContactPoint.ShapeContactNormal);   // 表面法线 n_c

	// 根据 BarycentricT 从两端粒子插值得到当前 segment 上的接触位置
	const float OneMinusT = 1.0f - BarycentricT;
	const FVector ContactPos = X0 * OneMinusT + X1 * BarycentricT;

	// 计算约束值: C = (ContactPos - SurfacePoint) · SurfaceNormal
	// C < 0 表示粒子在碰撞体内部（穿透）
	const float C = FVector::DotProduct(ContactPos - SurfacePoint, SurfaceNormal);

	// 只有穿透时才修正（C < 0 表示在内部）
	if (C >= 0.0f)
	{
		return;
	}

	// ====== 计算 barycentric effective mass ======
	// W = w0 * (1-t)^2 + w1 * t^2 + RBEffectiveW
	float RBEffectiveW = 0.0f;

	if (DynamicRBIndex >= 0 && DynamicRBIndex < DynamicRBs.Num())
	{
		const FDynamicRigidbodyProxy& RB = DynamicRBs[DynamicRBIndex];
		if (!RB.bIsKinematic)
		{
			RBEffectiveW = RB.GetEffectiveInverseMass(SurfaceNormal, SurfacePoint);
		}
	}

	const float TotalW = W0 * OneMinusT * OneMinusT
	                    + W1 * BarycentricT * BarycentricT
	                    + RBEffectiveW;

	if (TotalW < SMALL_NUMBER)
	{
		return;
	}

	// ====== 计算拉格朗日乘子增量 ======
	const float DeltaLambdaRaw = -C / TotalW;

	// 累积并钳制（单向约束：只推开，不拉近）
	const float NewLambda = FMath::Max(NormalLambda + DeltaLambdaRaw, 0.0f);
	const float DeltaLambda = NewLambda - NormalLambda;
	NormalLambda = NewLambda;

	if (FMath::Abs(DeltaLambda) < SMALL_NUMBER)
	{
		return;
	}

	// ====== 应用位置修正到两个端点粒子（按 barycentric 比例分配）======
	// Δx0 = w0 * (1-t) * λ * n
	// Δx1 = w1 * t * λ * n
	if (W0 > 0.0f)
	{
		PredictedPositions[ParticleIndex0] += W0 * OneMinusT * DeltaLambda * SurfaceNormal;
	}
	if (W1 > 0.0f)
	{
		PredictedPositions[ParticleIndex1] += W1 * BarycentricT * DeltaLambda * SurfaceNormal;
	}

	// ====== 计算并累积冲量到刚体 ======
	if (DynamicRBIndex >= 0 && DynamicRBIndex < DynamicRBs.Num())
	{
		FDynamicRigidbodyProxy& RB = DynamicRBs[DynamicRBIndex];
		if (!RB.bIsKinematic)
		{
			// 冲量 J = -λ · n / Δt（反向施加给刚体）
			const FVector Impulse = -DeltaLambda * SurfaceNormal / DeltaTime;
			RB.AccumulateImpulse(Impulse, SurfacePoint);
		}
	}

	// ====== 记录碰撞信息到两个端点粒子（用于摩擦计算）======
	// 两个端点粒子都标记为与该碰撞体碰撞
	if (W0 > 0.0f)
	{
		OutParticleColliderMapping[ParticleIndex0] = ColliderIndex;
		OutParticleCollisionNormal[ParticleIndex0] = SurfaceNormal;
	}
	if (W1 > 0.0f)
	{
		OutParticleColliderMapping[ParticleIndex1] = ColliderIndex;
		OutParticleCollisionNormal[ParticleIndex1] = SurfaceNormal;
	}
}

} // namespace Eden
