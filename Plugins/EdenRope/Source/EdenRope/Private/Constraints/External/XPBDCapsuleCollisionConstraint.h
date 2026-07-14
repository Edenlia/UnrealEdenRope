// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigidBodyProxy/RigidBodyProxy.h"
#include "Chaos/Collision/ContactPoint.h"

namespace Eden
{

/**
 * XPBD Per-Segment Capsule 碰撞约束
 * 将 segment（两个相邻粒子构成的 capsule）与 Chaos 刚体碰撞体的碰撞约束建模
 * 使用 barycentric 参数 t 将位移修正分配到 segment 的两个端点粒子
 * 支持静态碰撞体和动态刚体（双向反作用力）
 * 每帧动态生成
 */
class EDENROPE_API FXPBDCapsuleCollisionConstraint
{
public:
	FXPBDCapsuleCollisionConstraint() = default;

	/**
	 * 构造函数（静态碰撞体）
	 * @param InIdx0 segment 第一个端点粒子全局索引
	 * @param InIdx1 segment 第二个端点粒子全局索引
	 * @param InContactPoint 碰撞接触数据（世界坐标，单位：m）
	 * @param InBarycentricT contact point 在 segment 上的 barycentric 参数 t∈[0,1]
	 * @param InColliderIndex Collider 索引（用于摩擦）
	 */
	FXPBDCapsuleCollisionConstraint(
		uint32 InIdx0,
		uint32 InIdx1,
		const Chaos::FContactPoint& InContactPoint,
		float InBarycentricT,
		int32 InColliderIndex
	);

	/**
	 * 构造函数（动态碰撞体）
	 * @param InIdx0 segment 第一个端点粒子全局索引
	 * @param InIdx1 segment 第二个端点粒子全局索引
	 * @param InContactPoint 碰撞接触数据（世界坐标，单位：m）
	 * @param InBarycentricT contact point 在 segment 上的 barycentric 参数 t∈[0,1]
	 * @param InColliderIndex Collider 索引（用于摩擦）
	 * @param InDynamicRBIndex 动态刚体索引（在 DynamicRigidbodies 数组中）
	 */
	FXPBDCapsuleCollisionConstraint(
		uint32 InIdx0,
		uint32 InIdx1,
		const Chaos::FContactPoint& InContactPoint,
		float InBarycentricT,
		int32 InColliderIndex,
		int32 InDynamicRBIndex
	);

	/**
	 * 约束投影方法
	 * 使用 barycentric effective mass 将位移修正分配到两个端点粒子
	 * @param PredictedPositions 预测位置数组
	 * @param InvMass 粒子逆质量数组
	 * @param DynamicRBs 动态刚体数组（会累积冲量）
	 * @param DeltaTime 时间步长
	 * @param OutParticleColliderMapping 输出：粒子碰撞的 Collider 索引
	 * @param OutParticleCollisionNormal 输出：粒子碰撞的法线
	 */
	void Project(
		TArray<FVector>& PredictedPositions,
		const TArray<float>& InvMass,
		TArray<FDynamicRigidbodyProxy>& DynamicRBs,
		float DeltaTime,
		TArray<int32>& OutParticleColliderMapping,
		TArray<FVector>& OutParticleCollisionNormal
	);

	FORCEINLINE void ResetLambda() { NormalLambda = 0.0f; }

	uint32 ParticleIndex0 = 0;             // segment 第一个端点粒子全局索引
	uint32 ParticleIndex1 = 0;             // segment 第二个端点粒子全局索引
	Chaos::FContactPoint ContactPoint;     // 碰撞接触数据（世界坐标，单位：m）
	float BarycentricT = 0.0f;             // contact point 在 segment 上的 barycentric 参数 t∈[0,1]
	int32 ColliderIndex = -1;              // Collider 索引（用于摩擦）
	int32 DynamicRBIndex = -1;             // 动态刚体索引（-1 = 静态碰撞体）
	float NormalLambda = 0.0f;             // 累积拉格朗日乘子（单向约束，>= 0）
};

} // namespace Eden
