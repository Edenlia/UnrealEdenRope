// Fill out your copyright notice in the Description page of Project Settings.

#include "Constraints/RopeInternal/XPBDProjectionBendConstraint.h"
#include "Solver/XPBDEdenRopeEvolution.h"

Eden::FXPBDProjectionBendConstraint::FXPBDProjectionBendConstraint(FXPBDEdenRopeEvolution* InEvolution, uint32_t particleIndex1, uint32_t particleIndex2, uint32_t particleIndex3,
                                                                 float bendCompliance, float bendThreshold)
    : Evolution(InEvolution), ParticleIndex1(particleIndex1), ParticleIndex2(particleIndex2), ParticleIndex3(particleIndex3), Compliance(bendCompliance), BendThreshold(bendThreshold)
{
}

void Eden::FXPBDProjectionBendConstraint::Project(const TArray<FVector>& OriginalPositions, TArray<FVector>& PredictPositions,
                                                 const TArray<float>& InvMass, float DeltaTime, float DampingBeta)
{
	FVector& P1 = PredictPositions[ParticleIndex1];
	FVector& P2 = PredictPositions[ParticleIndex2];
	FVector& P3 = PredictPositions[ParticleIndex3];

	// 端点连线向量
	FVector Vs = P3 - P1;
	float S = Vs.Size();
	
	if (S < SMALL_NUMBER)
	{
		return; // 端点重合，无法计算
	}

	// P2 到 P1 的向量在端点连线上的投影
	FVector Vs1 = (P2 - P1).ProjectOnTo(Vs);
	float S1 = Vs1.Size();
	
	// 检查投影方向（如果投影在反方向则S1为负）
	if (FVector::DotProduct(Vs1, Vs) < 0)
	{
		S1 = -S1;
	}
	float S2 = S - S1;

	// P2 到端点连线的垂直向量（从P2指向投影点）
	FVector Ve = P1 + Vs1 - P2;
	float E = Ve.Size(); // 垂直距离 = 约束值 C

	if (E < SMALL_NUMBER)
	{
		return; // 已经在线上，无需修正
	}

	// 垂直方向单位向量
	FVector N = Ve / E;

	// 基于杠杆原理的梯度计算
	// dC/dP1 = (S2/S) * N  -- P1移动时，投影点变化与S2成正比
	// dC/dP2 = -N          -- P2直接影响垂直距离
	// dC/dP3 = (S1/S) * N  -- P3移动时，投影点变化与S1成正比
	float Ratio1 = S2 / S;
	float Ratio3 = S1 / S;

	FVector dCdP1 = Ratio1 * N;
	FVector dCdP2 = -N;
	FVector dCdP3 = Ratio3 * N;

	// 弯曲阈值：如果弯曲量在阈值内则不施加力，超过阈值只对超出部分施加力
	// C = E - BendThreshold, 只惩罚超出阈值的部分
	float C = FMath::Max(E - BendThreshold, 0.0f);

	float TildeAlpha = Compliance / (DeltaTime * DeltaTime);

	// 加权梯度平方和
	float SumInvMassGradient =
		InvMass[ParticleIndex1] * dCdP1.Dot(dCdP1) +
		InvMass[ParticleIndex2] * dCdP2.Dot(dCdP2) +
		InvMass[ParticleIndex3] * dCdP3.Dot(dCdP3);

	float dLambda;
	if (Evolution && Evolution->DampingMethod == EXPBDDampingMethod::Potential)
	{
		// Rayleigh dissipation potential damping
		float Gamma = Compliance * DampingBeta / DeltaTime;
		float SumVelocityGradient =
			dCdP1.Dot(PredictPositions[ParticleIndex1] - OriginalPositions[ParticleIndex1]) +
			dCdP2.Dot(PredictPositions[ParticleIndex2] - OriginalPositions[ParticleIndex2]) +
			dCdP3.Dot(PredictPositions[ParticleIndex3] - OriginalPositions[ParticleIndex3]);

		dLambda = (-C - TildeAlpha * Lambda - Gamma * SumVelocityGradient) /
			((1.0f + Gamma) * SumInvMassGradient + TildeAlpha + SMALL_NUMBER);
	}
	else
	{
		// No damping
		dLambda = (-C - TildeAlpha * Lambda) /
			(SumInvMassGradient + TildeAlpha + SMALL_NUMBER);
	}
	Lambda += dLambda;

	P1 += InvMass[ParticleIndex1] * dLambda * dCdP1;
	P2 += InvMass[ParticleIndex2] * dLambda * dCdP2;
	P3 += InvMass[ParticleIndex3] * dLambda * dCdP3;
}
