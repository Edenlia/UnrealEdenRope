// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * XPBD BendTwist 约束（POBCR 模型）
 * 连接相邻段方向 q[k] 和 q[k+1]
 * 控制弯曲和扭转自由度
 *
 * Darboux 向量 = 相对旋转四元数的虚部，描述曲率/扭率
 * Compliance 分量：(Bend1, Bend2, Twist) 对应 (X, Y, Z) 材质轴
 */
struct EDENROPE_API FXPBDBendTwistConstraint
{
	uint32  SegmentIndex1;
	uint32  SegmentIndex2;
	FQuat   RestDarboux;  // 静止 Darboux 向量（w=0，xyz=曲率/扭率）
	FVector Compliance;   // (Twist, Bend1, Bend2) compliance alpha - unit 1 / ([M]*[L]*[L])
	FVector Lambda;       // XPBD 累积拉格朗日乘子

	FXPBDBendTwistConstraint(uint32 Seg1, uint32 Seg2, FQuat InRest, FVector InCompliance)
		: SegmentIndex1(Seg1), SegmentIndex2(Seg2)
		, RestDarboux(InRest), Compliance(InCompliance), Lambda(FVector::ZeroVector)
	{}

	/**
	 * 投影约束（仅修正段方向）
	 * @param OrigOrient 约束求解前的段方向（用于 XPBD 阻尼，此处取 snapshot q0/q1）
	 * @param PredOrient 预测段方向（in/out）
	 * @param InvRotMass 段逆转动惯量
	 * @param Dt         时间步长（s）
	 */
	void Project(const TArray<FQuat>& OrigOrient, TArray<FQuat>& PredOrient,
	             const TArray<float>& InvRotMass, float Dt);

	FORCEINLINE void ResetLambda() { Lambda = FVector::ZeroVector; }
};
