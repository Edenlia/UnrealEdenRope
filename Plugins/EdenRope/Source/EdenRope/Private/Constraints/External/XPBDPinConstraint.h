// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class FXPBDEdenRopeEvolution;

namespace Eden {

struct FDynamicRigidbodyProxy;

/**
 * XPBD Pin 约束
 * 将粒子固定到世界空间位置或动态刚体上的某点
 * 支持对动态刚体施加反作用力（冲量）
 */
class EDENROPE_API FXPBDPinConstraint
{
public:
	/**
	 * 构造函数（世界空间固定点）
	 * @param InEvolution Evolution 指针
	 * @param InParticleIndex 粒子索引
	 * @param InWorldPosition 世界空间固定位置（单位：m）
	 * @param InCompliance 柔度参数
	 */
	FXPBDPinConstraint(
		FXPBDEdenRopeEvolution* InEvolution, 
		uint32 InParticleIndex, 
		const FVector& InWorldPosition, 
		float InCompliance = 0.0f
	);

	/**
	 * 构造函数（连接到动态刚体）
	 * @param InEvolution Evolution 指针
	 * @param InParticleIndex 粒子索引
	 * @param InDynamicRBIndex 动态刚体索引（在 DynamicRigidbodies 数组中）
	 * @param InLocalOffset 相对于刚体的局部空间偏移（单位：m）
	 * @param InCompliance 柔度参数
	 */
	FXPBDPinConstraint(
		FXPBDEdenRopeEvolution* InEvolution,
		uint32 InParticleIndex,
		int32 InDynamicRBIndex,
		const FVector& InLocalOffset,
		float InCompliance = 0.0f
	);

	~FXPBDPinConstraint() = default;

	/**
	 * XPBD 约束投影方法
	 * @param OriginalPositions 原始位置数组
	 * @param PredictPositions 预测位置数组（会被修改）
	 * @param InvMass 粒子逆质量数组
	 * @param DynamicRBs 动态刚体数组（会累积冲量）
	 * @param DeltaTime 时间步长
	 * @param DampingBeta 阻尼参数
	 * @param PredSegOrientations 预测段方向数组（可选，仅 SegmentOrientationIndex != INDEX_NONE 时使用）
	 * @param SegInvRotMass 段逆转动惯量数组（可选）
	 */
	void Project(
		const TArray<FVector>& OriginalPositions,
		TArray<FVector>& PredictPositions,
		const TArray<float>& InvMass,
		TArray<FDynamicRigidbodyProxy>& DynamicRBs,
		float DeltaTime,
		float DampingBeta,
		TArray<FQuat>* PredSegOrientations = nullptr,
		const TArray<float>* SegInvRotMass = nullptr
	);

	/**
	 * 更新世界空间位置（仅当 DynamicRBIndex < 0 时有效）
	 * @param NewWorldPosition 新的世界空间位置（单位：m）
	 */
	void UpdateWorldPosition(const FVector& NewWorldPosition);

	/**
	 * 更新目标朝向（仅当 SegmentOrientationIndex != INDEX_NONE 时有效）
	 * @param NewTargetOrientation 新的目标旋转（世界空间）
	 */
	void UpdateTargetOrientation(const FQuat& NewTargetOrientation);

	/**
	 * 计算当前目标位置（世界空间）
	 * @param DynamicRBs 动态刚体数组
	 * @return 目标位置（单位：m）
	 */
	FVector GetTargetPosition(const TArray<FDynamicRigidbodyProxy>& DynamicRBs) const;

	FORCEINLINE void ResetLambda()
	{
		Lambda = 0.0f;
		LambdaRot[0] = LambdaRot[1] = LambdaRot[2] = 0.0f;
	}

	// ===== 成员变量 =====
	FXPBDEdenRopeEvolution* Evolution = nullptr;
	uint32 ParticleIndex = 0;

	// 关联的刚体索引（如果是动态 RB 则 >= 0，如果是静态/世界则 -1）
	int32 DynamicRBIndex = -1;

	// 刚体局部空间的偏移（当 DynamicRBIndex >= 0 时使用）
	FVector LocalOffset = FVector::ZeroVector;

	// 世界空间位置（当 DynamicRBIndex < 0 时使用）
	FVector WorldPosition = FVector::ZeroVector;

	float Compliance = 0.0f;  // alpha
	float Lambda = 0.0f;

	// ===== 旋转约束字段（Rod Pin 专用）=====
	// INDEX_NONE = 无旋转约束（Rope 模式）
	int32  SegmentOrientationIndex = INDEX_NONE;
	// 绑定时段方向相对于 Pin 目标朝向的偏差（Darboux 公式中的静止值）
	FQuat  RestDarboux = FQuat(0.f, 0.f, 0.f, 0.f);
	// Pin 目标旋转（世界空间，每帧由组件更新）
	FQuat  TargetOrientation = FQuat::Identity;
	// 旋转 compliance（0 = 完全固定）
	float  RotationalCompliance = 0.f;
	// XPBD 旋转 lambda 累计（三个旋转分量）
	float  LambdaRot[3] = { 0.f, 0.f, 0.f };
};

} // namespace Eden
