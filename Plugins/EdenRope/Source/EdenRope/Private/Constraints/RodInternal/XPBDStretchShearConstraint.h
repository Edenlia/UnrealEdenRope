// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * XPBD StretchShear 约束（POBCR 模型）
 * 连接相邻粒子 p[k] 和 p[k+1] 以及段方向 q[k]
 * 控制拉伸和剪切自由度
 *
 * 材质坐标系：Z = 切向（段方向），X/Y = 截面法线
 * Compliance 分量：(Shear1, Shear2, Stretch) 对应 (X, Y, Z) 材质轴
 */
struct EDENROPE_API FXPBDStretchShearConstraint
{
	uint32  ParticleIndex1;
	uint32  ParticleIndex2;
	uint32  SegmentIndex;
	float   RestLength;    // 静止长度 unit: [L]
	FVector Compliance;    // (Shear1, Shear2, Stretch) compliance per material axis - unit: 1 / ([M]*[L]*[L])
	FVector Lambda;        // XPBD 累积拉格朗日乘子, 由于Constraint是Vector Constraint，这个也是三维的

	FXPBDStretchShearConstraint(uint32 P1, uint32 P2, uint32 Seg, float Length, FVector InCompliance)
		: ParticleIndex1(P1), ParticleIndex2(P2), SegmentIndex(Seg)
		, RestLength(Length), Compliance(InCompliance), Lambda(FVector::ZeroVector)
	{}

	/**
	 * 投影约束（同时修正位置和段方向）
	 * @param OrigPos    约束求解前的位置（用于 XPBD 阻尼，此处未用到）
	 * @param PredPos    预测位置（in/out）
	 * @param InvMass    粒子逆质量
	 * @param OrigOrient 约束求解前的段方向（用于 XPBD 阻尼，此处未用到）
	 * @param PredOrient 预测段方向（in/out）
	 * @param InvRotMass 段逆转动惯量 unit: 1 / ([M]*[L]*[L])
	 * @param Dt         时间步长 unit: [T]
	 */
	void Project(
		const TArray<FVector>& OrigPos, TArray<FVector>& PredPos,
		const TArray<float>&   InvMass,
		const TArray<FQuat>&   OrigOrient, TArray<FQuat>& PredOrient,
		const TArray<float>&   InvRotMass, float Dt);

	FORCEINLINE void ResetLambda() { Lambda = FVector::ZeroVector; }
};
