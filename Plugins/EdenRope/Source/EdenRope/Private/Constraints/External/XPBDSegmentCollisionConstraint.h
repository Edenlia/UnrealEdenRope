// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/Collision/ContactPoint.h"

namespace Eden {

/**
 * XPBD Segment-to-Segment (Capsule-Capsule) 碰撞约束
 * 每个 segment 由两个相邻粒子构成胶囊体，使用 barycentric effective mass 分配修正到 4 个端点粒子
 * 每帧动态生成，用于自碰撞和跨绳碰撞
 * 接触数据（法线、barycentric 参数、RestDistance）由外部预计算后传入
 */
class EDENROPE_API FXPBDSegmentCollisionConstraint
{
public:
	FXPBDSegmentCollisionConstraint() = default;
	FXPBDSegmentCollisionConstraint(
		int32 InA0, int32 InA1,                    // segA 端点全局索引
		int32 InB0, int32 InB1,                    // segB 端点全局索引
		const Chaos::FContactPoint& InContactPoint, // 接触数据（法线、穿透深度）
		float InBarycentricS,                       // segA 上的 barycentric 参数
		float InBarycentricT,                       // segB 上的 barycentric 参数
		float InRestDistance);                       // 安全距离 = rA + rB

	/**
	 * 约束投影
	 * 使用预计算的 barycentric 参数和接触法线进行位置修正
	 */
	void Project(
		TArray<FVector>& PredictedPositions,
		const TArray<float>& InvMass);

	FORCEINLINE void ResetLambda() { NormalLambda = 0.0f; }

	int32 ParticleIndexA0 = 0;
	int32 ParticleIndexA1 = 0;
	int32 ParticleIndexB0 = 0;
	int32 ParticleIndexB1 = 0;
	Chaos::FContactPoint ContactPoint;  // 接触数据（法线、穿透深度）
	float BarycentricS = 0.0f;          // segA 上最近点的 barycentric 参数
	float BarycentricT = 0.0f;          // segB 上最近点的 barycentric 参数
	float RestDistance = 0.0f;           // 安全距离 = rA + rB
	float NormalLambda = 0.0f;          // 累积拉格朗日乘子（单向约束，>= 0）
};

} // namespace Eden
