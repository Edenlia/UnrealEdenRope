// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

class FXPBDEdenRopeEvolution;

namespace Eden {
class EDENROPE_API FXPBDDistanceConstraint
{
public:
	const float OVER_RELAXATION_COEFFICIENT = 1.0f/1.015f; // over relaxation: 1/omega
	
	FXPBDDistanceConstraint(FXPBDEdenRopeEvolution* InEvolution, uint32_t particleIndex1, uint32_t particleIndex2, float length, float compliance = 0.0001f, float compressThreshold = 0.0f);
	~FXPBDDistanceConstraint() = default;

	// XPBD约束投影方法
	void Project(const TArray<FVector>& OriginalPositions, TArray<FVector>& PredictPositions,
		const TArray<float>& InvMass, float DeltaTime, float DampingBeta);

	FORCEINLINE void ResetLambda() { Lambda = 0.0f; }

	FXPBDEdenRopeEvolution* Evolution = nullptr;
	uint32_t ParticleIndex1;
	uint32_t ParticleIndex2;
	float Length;
	float Compliance; // alpha - unit: 1/[M]
	float CompressThreshold; // 压缩阈值，如果DistanceConstraint是压缩, 则量在Threshold之下时不给力
	float Lambda = 0.0f;
};
}
