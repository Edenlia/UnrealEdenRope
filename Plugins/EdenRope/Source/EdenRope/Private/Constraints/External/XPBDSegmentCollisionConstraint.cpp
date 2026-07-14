// Copyright Eden Games. All Rights Reserved.

#include "Constraints/External/XPBDSegmentCollisionConstraint.h"

Eden::FXPBDSegmentCollisionConstraint::FXPBDSegmentCollisionConstraint(
	int32 InA0, int32 InA1,
	int32 InB0, int32 InB1,
	const Chaos::FContactPoint& InContactPoint,
	float InBarycentricS,
	float InBarycentricT,
	float InRestDistance)
	: ParticleIndexA0(InA0)
	, ParticleIndexA1(InA1)
	, ParticleIndexB0(InB0)
	, ParticleIndexB1(InB1)
	, ContactPoint(InContactPoint)
	, BarycentricS(InBarycentricS)
	, BarycentricT(InBarycentricT)
	, RestDistance(InRestDistance)
{
}

void Eden::FXPBDSegmentCollisionConstraint::Project(
	TArray<FVector>& PredictedPositions,
	const TArray<float>& InvMass)
{
	const FVector& A0 = PredictedPositions[ParticleIndexA0];
	const FVector& A1 = PredictedPositions[ParticleIndexA1];
	const FVector& B0 = PredictedPositions[ParticleIndexB0];
	const FVector& B1 = PredictedPositions[ParticleIndexB1];

	// 使用预计算的 barycentric 参数重新计算当前迭代的最近点位置
	const FVector C1 = FMath::Lerp(A0, A1, BarycentricS);
	const FVector C2 = FMath::Lerp(B0, B1, BarycentricT);

	const FVector Delta = C1 - C2;
	const float Dist = Delta.Size();

	if (Dist < SMALL_NUMBER)
	{
		return;
	}

	// 使用预计算的法线方向（从 B 指向 A）
	const FVector Normal = FVector(ContactPoint.ShapeContactNormal);

	// 有符号距离：C = |C1 - C2| - RestDistance，< 0 表示穿透
	const float Distance = Dist - RestDistance;

	if (Distance >= 0.0f)
	{
		return;
	}

	// Barycentric effective mass
	const float WA0 = InvMass[ParticleIndexA0];
	const float WA1 = InvMass[ParticleIndexA1];
	const float WB0 = InvMass[ParticleIndexB0];
	const float WB1 = InvMass[ParticleIndexB1];

	const float OneMinusS = 1.0f - BarycentricS;
	const float OneMinusT = 1.0f - BarycentricT;

	const float W = WA0 * OneMinusS * OneMinusS
	              + WA1 * BarycentricS * BarycentricS
	              + WB0 * OneMinusT * OneMinusT
	              + WB1 * BarycentricT * BarycentricT;

	if (W < SMALL_NUMBER)
	{
		return;
	}

	// 拉格朗日乘子增量
	const float DeltaLambdaRaw = -Distance / W;

	// 累积并钳制（单向约束：只推开，不拉近）
	const float NewLambda = FMath::Max(NormalLambda + DeltaLambdaRaw, 0.0f);
	const float DeltaLambda = NewLambda - NormalLambda;
	NormalLambda = NewLambda;

	if (FMath::Abs(DeltaLambda) < SMALL_NUMBER)
	{
		return;
	}

	// 应用位置修正到 4 个端点
	PredictedPositions[ParticleIndexA0] += DeltaLambda * WA0 * OneMinusS * Normal;
	PredictedPositions[ParticleIndexA1] += DeltaLambda * WA1 * BarycentricS * Normal;
	PredictedPositions[ParticleIndexB0] -= DeltaLambda * WB0 * OneMinusT * Normal;
	PredictedPositions[ParticleIndexB1] -= DeltaLambda * WB1 * BarycentricT * Normal;
}