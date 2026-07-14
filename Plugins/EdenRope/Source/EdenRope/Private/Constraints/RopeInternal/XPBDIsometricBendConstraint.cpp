// Fill out your copyright notice in the Description page of Project Settings.


#include "Constraints/RopeInternal/XPBDIsometricBendConstraint.h"
#include "Solver/XPBDEdenRopeEvolution.h"

Eden::FXPBDIsometricBendConstraint::FXPBDIsometricBendConstraint(FXPBDEdenRopeEvolution* InEvolution, uint32_t particleIndex1, uint32_t particleIndex2, uint32_t particleIndex3,
                                                                 float bendCompliance, float bendThreshold)
    : Evolution(InEvolution), ParticleIndex1(particleIndex1), ParticleIndex2(particleIndex2), ParticleIndex3(particleIndex3), Compliance(bendCompliance), BendThreshold(bendThreshold){
}

void Eden::FXPBDIsometricBendConstraint::Project(const TArray<FVector>& OriginalPositions, TArray<FVector>& PredictPositions,
                                                 		const TArray<float>& InvMass, float DeltaTime, float DampingBeta)
{
	FVector& P1 = PredictPositions[ParticleIndex1];
	FVector& P2 = PredictPositions[ParticleIndex2];
	FVector& P3 = PredictPositions[ParticleIndex3];

	FVector U = P2 - (P1 + P2 + P3) / 3.0f;
    float Bend = U.Size();

    FVector N = U / (Bend + SMALL_NUMBER);

    FVector dCdP1 = -1.0f/3.0f * N;
    FVector dCdP2 =  2.0f/3.0f * N;
    FVector dCdP3 = -1.0f/3.0f * N;

    // 弯曲阈值：如果弯曲量在阈值内则不施加力，超过阈值只对超出部分施加力
    // C = Bend - BendThreshold, 只惩罚超出阈值的部分
    float C = FMath::Max(Bend - BendThreshold, 0.0f);

	float TildeAlpha = Compliance / (DeltaTime * DeltaTime);

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