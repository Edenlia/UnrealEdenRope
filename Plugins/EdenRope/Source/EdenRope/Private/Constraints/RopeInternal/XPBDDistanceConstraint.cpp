// Fill out your copyright notice in the Description page of Project Settings.

#include "Constraints/RopeInternal/XPBDDistanceConstraint.h"
#include "Solver/XPBDEdenRopeEvolution.h"

Eden::FXPBDDistanceConstraint::FXPBDDistanceConstraint(FXPBDEdenRopeEvolution* InEvolution, uint32_t particleIndex1, uint32_t particleIndex2, float length, float compliance, float compressThreshold)
    : Evolution(InEvolution), ParticleIndex1(particleIndex1), ParticleIndex2(particleIndex2), Length(length), Compliance(compliance), CompressThreshold(compressThreshold)
{
    
}

void Eden::FXPBDDistanceConstraint::Project(
    const TArray<FVector>& OriginalPositions, 
    TArray<FVector>& PredictPositions,
    const TArray<float>& InvMass,
    float DeltaTime,
    float DampingBeta)
{
	FVector& P1 = PredictPositions[ParticleIndex1];
	FVector& P2 = PredictPositions[ParticleIndex2];

    const FVector Delta = P1 - P2;
    float D = Delta.Size();

    float C = D - OVER_RELAXATION_COEFFICIENT * Length;

    // 压缩阈值：如果压缩量在阈值内则不施加力，超过阈值只对超出部分施加力
    // C < 0 表示压缩，|C| <= CompressThreshold 时 C 变为 0
    // |C| > CompressThreshold 时 C = C + CompressThreshold（只惩罚超出部分）
    C -= FMath::Max(FMath::Min(C, 0.0f), -CompressThreshold);

    FVector dCdP1 = Delta / (D + SMALL_NUMBER);
    FVector dCdP2 = -dCdP1;

    float TildeAlpha = Compliance / (DeltaTime * DeltaTime);
    float SumInvMassGradient = InvMass[ParticleIndex1] * dCdP1.Dot(dCdP1) + InvMass[ParticleIndex2] * dCdP2.Dot(dCdP2);

    float dLambda;
    if (Evolution && Evolution->DampingMethod == EXPBDDampingMethod::Potential)
    {
        // Rayleigh dissipation potential damping
        float Gamma = Compliance * DampingBeta / DeltaTime;
        FVector dP1 = PredictPositions[ParticleIndex1] - OriginalPositions[ParticleIndex1];
        FVector dP2 = PredictPositions[ParticleIndex2] - OriginalPositions[ParticleIndex2];
        float SumVelocityGradient = dCdP1.Dot(dP1) + dCdP2.Dot(dP2);

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
}
