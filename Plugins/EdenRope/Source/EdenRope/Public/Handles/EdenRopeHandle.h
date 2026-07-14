// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenRopeHandle.generated.h"

/**
 * 绳索创建参数
 * Component 配置参数，传给 Solver 用于创建粒子和约束
 */
USTRUCT(BlueprintType)
struct EDENROPE_API FEdenRopeCreationParams
{
	GENERATED_BODY()

	// 基础参数
	int32 NumParticles = 10;
	float LengthCm = 100.0f;          // cm，Solver 内部转 m
	float MassPerParticle = 1.0f;
	
	// Distance 约束
	float DistanceStiffness = 1.0f;
	float DistanceCompressThresholdCm = 0.0f; // 压缩阈值 (cm)，Solver 内部转 m
	// Bend 约束
	float BendStiffness = 1.0f;
	float MinBendAngleCoefficient = 0.1f; // 最小弯曲角度系数，C < Coefficient * SegmentLength 时不施加约束

	// 碰撞
	float ParticleRadiusCm = 1.0f; // 粒子半径 (cm)，用于碰撞检测

	// 世界空间缩放（由组件设置，Solver 侧在创建粒子和约束时应用）
	// LengthCm / ParticleRadiusCm 等保持为原始局部空间值，Solver 通过此字段计算实际缩放后的值
	FVector WorldScale3D = FVector::OneVector;

	// Rod 模式参数（RegisterRope 忽略，RegisterRod 使用）
	float StretchCompliance  = 0.0f;
	float Shear1Compliance   = 0.0f;
	float Shear2Compliance   = 0.0f;
	float TwistCompliance    = 0.0f;
	float Bend1Compliance    = 0.0f;
	float Bend2Compliance    = 0.0f;
	float SegmentInertiaPerUnitLength = 1.0f;

	// Rod 初始形状保持（true = RestDarboux 从初始曲率计算，false = RestDarboux 为 0，平衡态为直线）
	bool bKeepInitialShape = false;

	// ===== Per-Component Friction =====
	float GroundFrictionProportional = 0.95f;
	float GroundFrictionConstant = 0.01f;

	// ===== Per-Component Self Collision =====
	bool bEnableSelfCollision = false;
	int32 SelfCollisionSkipCount = 3;
};

/**
 * Handle 用于让组件引用 Solver 中的数据
 * 索引固定不变，删除时只标记失效
 */
USTRUCT(BlueprintType)
struct EDENROPE_API FEdenRopeHandle
{
	GENERATED_BODY()

	// 粒子在全局数组中的起始索引（注册时分配，永不改变）
	int32 ParticleStartIndex = INDEX_NONE;
	
	// 粒子数量
	int32 ParticleCount = 0;

	// 约束索引范围（全局数组中的位置）
	int32 DistanceConstraintStart = INDEX_NONE;
	int32 DistanceConstraintCount = 0;
	
	int32 BendConstraintStart = INDEX_NONE;
	int32 BendConstraintCount = 0;

	// Rod 专属字段（Rope handle 中为 INDEX_NONE/0）
	int32 SegmentOrientationStartIndex = INDEX_NONE;
	int32 SegmentOrientationCount      = 0;
	int32 StretchShearConstraintStart  = INDEX_NONE;
	int32 StretchShearConstraintCount  = 0;
	int32 BendTwistConstraintStart     = INDEX_NONE;
	int32 BendTwistConstraintCount     = 0;

	// 是否有效（删除时标记为 false）
	bool bValid = false;

	// 检查 Handle 是否有效
	bool IsValid() const
	{
		return bValid && ParticleStartIndex != INDEX_NONE;
	}

	// 重置 Handle
	void Reset()
	{
		ParticleStartIndex = INDEX_NONE;
		ParticleCount = 0;
		DistanceConstraintStart = INDEX_NONE;
		DistanceConstraintCount = 0;
		BendConstraintStart = INDEX_NONE;
		BendConstraintCount = 0;
		SegmentOrientationStartIndex = INDEX_NONE;
		SegmentOrientationCount      = 0;
		StretchShearConstraintStart  = INDEX_NONE;
		StretchShearConstraintCount  = 0;
		BendTwistConstraintStart     = INDEX_NONE;
		BendTwistConstraintCount     = 0;
		bValid = false;
	}

	// 将本地粒子索引转换为全局索引
	int32 ToGlobalIndex(int32 LocalIndex) const
	{
		if (LocalIndex < 0 || LocalIndex >= ParticleCount)
		{
			return INDEX_NONE;
		}
		return ParticleStartIndex + LocalIndex;
	}
};
