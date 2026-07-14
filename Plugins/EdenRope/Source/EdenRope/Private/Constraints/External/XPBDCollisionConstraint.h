// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigidBodyProxy/RigidBodyProxy.h"
#include "Chaos/Collision/ContactPoint.h"

namespace Eden
{

class EDENROPE_API FXPBDCollisionConstraint
{
public:
	FXPBDCollisionConstraint() = default;
	
	/**
	 * 构造函数（静态碰撞体）
	 */
	FXPBDCollisionConstraint(
		uint32 InParticleIndex,
		const Chaos::FContactPoint& InContactPoint,
		int32 InColliderIndex
	);

	/**
	 * 构造函数（动态碰撞体）
	 * @param InDynamicRBIndex 动态刚体索引（在 DynamicRigidbodies 数组中）
	 */
	FXPBDCollisionConstraint(
		uint32 InParticleIndex,
		const Chaos::FContactPoint& InContactPoint,
		int32 InColliderIndex,
		int32 InDynamicRBIndex
	);

	/**
	 * 约束投影方法（刚性约束，直接修正位置）
	 * @param PredictPositions 预测位置数组
	 * @param InvMass 粒子逆质量数组
	 * @param DynamicRBs 动态刚体数组（会累积冲量）
	 * @param DeltaTime 时间步长
	 * @param OutParticleColliderMapping 输出：粒子碰撞的 Collider 索引
	 * @param OutParticleCollisionNormal 输出：粒子碰撞的法线
	 */
	void Project(
		TArray<FVector>& PredictPositions,
		const TArray<float>& InvMass,
		TArray<FDynamicRigidbodyProxy>& DynamicRBs,
		float DeltaTime,
		TArray<int32>& OutParticleColliderMapping,
		TArray<FVector>& OutParticleCollisionNormal
	);

	uint32 ParticleIndex = 0;              // 碰撞的粒子索引
	Chaos::FContactPoint ContactPoint;     // 碰撞接触数据（世界坐标，单位：m）
	int32 ColliderIndex = -1;              // Collider 索引（用于摩擦）
	int32 DynamicRBIndex = -1;             // 动态刚体索引（-1 = 静态碰撞体）
};
} // namespace Eden
