#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UPrimitiveComponent;

namespace Eden
{

/**
 * 动态刚体代理（用于 Pin 约束和碰撞响应）
 * 存储刚体的物理属性和累积的速度变化量
 * 单位：质量 kg，距离 m，时间 s
 */
struct FDynamicRigidbodyProxy
{
	TWeakObjectPtr<UPrimitiveComponent> OwnerComponent;  // 拥有此刚体的组件

	// 物理属性（每帧从 UE 拉取或计算）
	float InverseMass = 0.0f;                            // 1/m
	FMatrix InverseInertiaTensor = FMatrix::Identity;    // I⁻¹ (世界空间)

	// 当前状态（每帧从 UE 拉取）
	FVector CenterOfMass = FVector::ZeroVector;          // 质心位置（世界坐标，单位：m）
	FVector PivotPosition = FVector::ZeroVector;         // 组件 pivot 世界坐标（单位：m）
	FQuat Rotation = FQuat::Identity;                    // 旋转
	FVector Velocity = FVector::ZeroVector;              // 线速度（单位：m/s）
	FVector AngularVelocity = FVector::ZeroVector;       // 角速度（单位：rad/s）

	// 累积的速度变化量（Evolution 维护，约束求解时累积）
	FVector DeltaLinearVelocity = FVector::ZeroVector;   // 线速度增量
	FVector DeltaAngularVelocity = FVector::ZeroVector;  // 角速度增量

	bool bIsKinematic = false;  // 是否是运动学物体（只影响绳子，不受绳子影响）

	FDynamicRigidbodyProxy() = default;

	FDynamicRigidbodyProxy(UPrimitiveComponent* InOwner)
		: OwnerComponent(InOwner)
	{
	}

	/** 重置速度增量（每帧开始时调用） */
	void ResetDeltas()
	{
		DeltaLinearVelocity = FVector::ZeroVector;
		DeltaAngularVelocity = FVector::ZeroVector;
	}

	/** 从 UE 的 PrimitiveComponent 拉取状态 */
	void UpdateFromUnreal();

	/** 将累积的速度增量应用到 UE 的 PrimitiveComponent */
	void ApplyDeltasToUnreal();

	/**
	 * 计算给定方向上的有效逆质量
	 * @param Direction 约束方向（单位向量）
	 * @param WorldPoint 力作用点（世界坐标，单位：m）
	 * @return 有效逆质量 = 1/m + (r×n)ᵀ · I⁻¹ · (r×n)
	 */
	float GetEffectiveInverseMass(const FVector& Direction, const FVector& WorldPoint) const;

	/**
	 * 累积冲量产生的速度变化
	 * @param Impulse 冲量向量（单位：kg·m/s）
	 * @param WorldPoint 冲量作用点（世界坐标，单位：m）
	 */
	void AccumulateImpulse(const FVector& Impulse, const FVector& WorldPoint);
};

/**
 * Box 碰撞体代理（世界坐标，单位：m）
 * 支持 OBB（Oriented Bounding Box）碰撞检测
 */
struct FRBBoxProxy
{
	FVector Center;      // 世界坐标中心
	FVector HalfExtents; // 半尺寸（局部空间）
	FQuat Rotation;      // 世界旋转
	TWeakObjectPtr<UPrimitiveComponent> OwnerComponent;  // 拥有此碰撞体的组件（用于后续反作用力）

	FRBBoxProxy()
		: Center(FVector::ZeroVector)
		, HalfExtents(FVector::ZeroVector)
		, Rotation(FQuat::Identity)
		, OwnerComponent(nullptr)
	{
	}

	FRBBoxProxy(const FVector& InCenter, const FVector& InHalfExtents, const FQuat& InRotation, UPrimitiveComponent* InOwner = nullptr)
		: Center(InCenter)
		, HalfExtents(InHalfExtents)
		, Rotation(InRotation)
		, OwnerComponent(InOwner)
	{
	}
};

/**
 * Sphere 碰撞体代理（世界坐标，单位：m）
 */
struct FRBSphereProxy
{
	FVector Center;  // 世界坐标中心
	float Radius;    // 半径
	TWeakObjectPtr<UPrimitiveComponent> OwnerComponent;  // 拥有此碰撞体的组件（用于后续反作用力）

	FRBSphereProxy()
		: Center(FVector::ZeroVector)
		, Radius(0.0f)
		, OwnerComponent(nullptr)
	{
	}

	FRBSphereProxy(const FVector& InCenter, float InRadius, UPrimitiveComponent* InOwner = nullptr)
		: Center(InCenter)
		, Radius(InRadius)
		, OwnerComponent(InOwner)
	{
	}
};

/**
 * 所有碰撞体代理的容器
 */
struct FRigidBodyProxies
{
	TArray<FRBBoxProxy> Boxes;
	TArray<FRBSphereProxy> Spheres;

	void Reset()
	{
		Boxes.Reset();
		Spheres.Reset();
	}

	int32 GetTotalCount() const
	{
		return Boxes.Num() + Spheres.Num();
	}
};

// TODO: 后续扩展
struct FRBConvexProxy
{
};

struct FRBHeightFieldProxy
{
};

}
