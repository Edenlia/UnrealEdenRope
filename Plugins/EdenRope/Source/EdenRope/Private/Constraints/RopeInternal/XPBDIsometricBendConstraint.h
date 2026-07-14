// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class FXPBDEdenRopeEvolution;

namespace Eden {
class EDENROPE_API FXPBDIsometricBendConstraint
{
public:
	FXPBDIsometricBendConstraint(FXPBDEdenRopeEvolution* InEvolution, uint32_t particleIndex1, uint32_t particleIndex2, uint32_t particleIndex3, 
					float bendCompliance = 0.0001f, float bendThreshold = 0.0f);
	~FXPBDIsometricBendConstraint() = default;

	// XPBD约束投影方法
	void Project(const TArray<FVector>& OriginalPositions, TArray<FVector>& PredictPositions,
		const TArray<float>& InvMass, float DeltaTime, float DampingBeta);

	FORCEINLINE void ResetLambda() { Lambda = 0.0f; }

	FXPBDEdenRopeEvolution* Evolution = nullptr;
	uint32_t ParticleIndex1; // 第一个粒子
	uint32_t ParticleIndex2; // 中间粒子
	uint32_t ParticleIndex3; // 第三个粒子
	float Compliance = 0.01f; // alpha - unit: 1/[M]
	float BendThreshold = 0.0f; // 弯曲阈值，C < Threshold 时不施加约束
	float Lambda = 0.0f;
};
} 