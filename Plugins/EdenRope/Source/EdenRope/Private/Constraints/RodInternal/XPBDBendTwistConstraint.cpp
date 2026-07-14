// Copyright Eden Games. All Rights Reserved.

#include "XPBDBendTwistConstraint.h"

void FXPBDBendTwistConstraint::Project(
	const TArray<FQuat>& OrigOrient, TArray<FQuat>& PredOrient,
	const TArray<float>& InvRotMass, float Dt)
{
	const int32 Seg1 = (int32)SegmentIndex1;
	const int32 Seg2 = (int32)SegmentIndex2;

	if (!PredOrient.IsValidIndex(Seg1) || !PredOrient.IsValidIndex(Seg2))
		return;

	// 注意：使用求解前的 q0/q1 快照（XPBD 要求使用约束前状态计算修正量）
	const FQuat q0 = PredOrient[Seg1];
	const FQuat q1 = PredOrient[Seg2];

	// Darboux 向量：相对旋转的虚部
	const FQuat  rel  = q0.Inverse() * q1;
	const FVector      curr(rel.X, rel.Y, rel.Z);
	FVector rest(RestDarboux.X, RestDarboux.Y, RestDarboux.Z);

	// 最短路径消歧：保证 curr 和 rest 在同一半球
	// 论文中使用 |\Omega-\Omega^0|<|\Omega+\Omega^0| 为正, 反之为负来判断
	// 等价于 \Omega\cdot\Omega^0>0 为正, 反之为负
	if (curr.Dot(rest) < 0.f)
	{
		rest = -rest;
	}
	
	const FVector error = curr - rest;
	const FVector alpha = Compliance / (Dt * Dt);
	const FVector TotalSumJacobiW     = FVector(InvRotMass[Seg1] + InvRotMass[Seg2]) + alpha;

	// Lagrange 乘子增量（component-wise）
	FVector dL;
	dL.X = (TotalSumJacobiW.X > KINDA_SMALL_NUMBER) ? -(error.X + alpha.X * Lambda.X) / TotalSumJacobiW.X : 0.f;
	dL.Y = (TotalSumJacobiW.Y > KINDA_SMALL_NUMBER) ? -(error.Y + alpha.Y * Lambda.Y) / TotalSumJacobiW.Y : 0.f;
	dL.Z = (TotalSumJacobiW.Z > KINDA_SMALL_NUMBER) ? -(error.Z + alpha.Z * Lambda.Z) / TotalSumJacobiW.Z : 0.f;
	Lambda += dL;

	const FQuat dLPure(dL.X, dL.Y, dL.Z, 0.f);

	// 段 1 方向修正（基于 q1 * dL）
	if (InvRotMass[Seg1] > 0.f)
	{
		const FQuat  q1dL = q1 * dLPure;
		const float  s0   = -InvRotMass[Seg1];
		PredOrient[Seg1] = FQuat(
			q0.X + q1dL.X * s0,
			q0.Y + q1dL.Y * s0,
			q0.Z + q1dL.Z * s0,
			q0.W + q1dL.W * s0
		).GetNormalized();
	}

	// 段 2 方向修正（基于 q0 * dL）
	if (InvRotMass[Seg2] > 0.f)
	{
		const FQuat  q0dL = q0 * dLPure;
		const float  s1   =  +InvRotMass[Seg2];
		PredOrient[Seg2] = FQuat(
			q1.X + q0dL.X * s1,
			q1.Y + q0dL.Y * s1,
			q1.Z + q0dL.Z * s1,
			q1.W + q0dL.W * s1
		).GetNormalized();
	}
}
