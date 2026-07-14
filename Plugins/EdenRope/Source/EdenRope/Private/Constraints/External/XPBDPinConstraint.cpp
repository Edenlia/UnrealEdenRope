// Fill out your copyright notice in the Description page of Project Settings.


#include "Constraints/External/XPBDPinConstraint.h"
#include "Solver/XPBDEdenRopeEvolution.h"
#include "RigidBodyProxy/RigidBodyProxy.h"


Eden::FXPBDPinConstraint::FXPBDPinConstraint(
	FXPBDEdenRopeEvolution* InEvolution, 
	uint32 InParticleIndex, 
	const FVector& InWorldPosition, 
	float InCompliance)
	: Evolution(InEvolution)
	, ParticleIndex(InParticleIndex)
	, DynamicRBIndex(-1)
	, LocalOffset(FVector::ZeroVector)
	, WorldPosition(InWorldPosition)
	, Compliance(InCompliance)
{
}

Eden::FXPBDPinConstraint::FXPBDPinConstraint(
	FXPBDEdenRopeEvolution* InEvolution,
	uint32 InParticleIndex,
	int32 InDynamicRBIndex,
	const FVector& InLocalOffset,
	float InCompliance)
	: Evolution(InEvolution)
	, ParticleIndex(InParticleIndex)
	, DynamicRBIndex(InDynamicRBIndex)
	, LocalOffset(InLocalOffset)
	, WorldPosition(FVector::ZeroVector)
	, Compliance(InCompliance)
{
}

void Eden::FXPBDPinConstraint::UpdateWorldPosition(const FVector& NewWorldPosition)
{
	if (DynamicRBIndex < 0)
	{
		WorldPosition = NewWorldPosition;
	}
}

void Eden::FXPBDPinConstraint::UpdateTargetOrientation(const FQuat& NewTargetOrientation)
{
	TargetOrientation = NewTargetOrientation;
}

FVector Eden::FXPBDPinConstraint::GetTargetPosition(const TArray<FDynamicRigidbodyProxy>& DynamicRBs) const
{
	if (DynamicRBIndex >= 0 && DynamicRBIndex < DynamicRBs.Num())
	{
		// 连接到动态刚体：LocalOffset 相对于组件 pivot，转换到世界空间
		const FDynamicRigidbodyProxy& RB = DynamicRBs[DynamicRBIndex];
		return RB.PivotPosition + RB.Rotation.RotateVector(LocalOffset);
	}
	else
	{
		// 连接到世界空间固定点
		return WorldPosition;
	}
}

void Eden::FXPBDPinConstraint::Project(
	const TArray<FVector>& OriginalPositions,
	TArray<FVector>& PredictPositions,
	const TArray<float>& InvMass,
	TArray<FDynamicRigidbodyProxy>& DynamicRBs,
	float DeltaTime,
	float DampingBeta,
	TArray<FQuat>* PredSegOrientations,
	const TArray<float>* SegInvRotMass)
{
	FVector& P = PredictPositions[ParticleIndex];

	// 计算目标位置（世界空间）
	// Fix A: 分离两个位置概念（Obi 风格）：
	//   CurrentContactPos - 当前帧接触点，用于有效质量和冲量力矩臂（数值稳定）
	//   TargetPos         - 预测到步末的目标位置，用于约束梯度（步内收敛）
	// Fix B: 旋转预测含 DeltaAngularVelocity，修正偏心附着点的预测误差
	FVector TargetPos;
	FVector CurrentContactPos;
	if (DynamicRBIndex >= 0 && DynamicRBIndex < DynamicRBs.Num())
	{
		const FDynamicRigidbodyProxy& RB = DynamicRBs[DynamicRBIndex];
		if (!RB.bIsKinematic)
		{
			// Fix A: 当前接触点（使用当前帧 Rotation，不含预测）
			CurrentContactPos = RB.PivotPosition + RB.Rotation.RotateVector(LocalOffset);

			// Fix B: 预测旋转（含已累积的 DeltaAngularVelocity）
			// q_dot = 0.5 * ω_quat * q，积分一步：q_new = normalize(q + dt * q_dot)
			const FVector PredictedAngular = RB.AngularVelocity + RB.DeltaAngularVelocity;
			const FQuat SpinDelta(
				PredictedAngular.X * 0.5f * DeltaTime,
				PredictedAngular.Y * 0.5f * DeltaTime,
				PredictedAngular.Z * 0.5f * DeltaTime,
				0.f);
			const FQuat SpinDeltaQ = SpinDelta * RB.Rotation;
			const FQuat PredictedRotation = FQuat(
				RB.Rotation.X + SpinDeltaQ.X,
				RB.Rotation.Y + SpinDeltaQ.Y,
				RB.Rotation.Z + SpinDeltaQ.Z,
				RB.Rotation.W + SpinDeltaQ.W
			).GetNormalized();

			// 预测目标：含线速度预测 + 旋转预测
			// Fix C: RB.Velocity is CoM velocity (UE Chaos CoM-based). Predict CoM first,
			// then derive Pivot prediction via rotated local offset from CoM to Pivot.
			const FVector PredictedVelocity = RB.Velocity + RB.DeltaLinearVelocity;
			const FVector PredictedCOM = RB.CenterOfMass + PredictedVelocity * DeltaTime;
			const FVector LocalComToPivot = RB.Rotation.UnrotateVector(RB.PivotPosition - RB.CenterOfMass);
			const FVector PredictedPivot = PredictedCOM + PredictedRotation.RotateVector(LocalComToPivot);
			TargetPos = PredictedPivot + PredictedRotation.RotateVector(LocalOffset);
		}
		else
		{
			// Kinematic：位置由外部驱动，直接用当前值
			TargetPos = GetTargetPosition(DynamicRBs);
			CurrentContactPos = TargetPos;
		}
	}
	else
	{
		// WorldPosition：固定点，无需预测
		TargetPos = GetTargetPosition(DynamicRBs);
		CurrentContactPos = TargetPos;
	}

	// 约束值和梯度
	FVector Gradient = P - TargetPos;
	float C = Gradient.Size();

	// 约束已满足, 并且不管旋转的情况下
	if (C < SMALL_NUMBER && SegmentOrientationIndex == INDEX_NONE)
	{
		return;
	}

	FVector GradientDir = Gradient / C;

	// ====== 计算有效逆质量 ======
	float ParticleW = InvMass[ParticleIndex];
	float RBEffectiveW = 0.0f;

	if (DynamicRBIndex >= 0 && DynamicRBIndex < DynamicRBs.Num())
	{
		const FDynamicRigidbodyProxy& RB = DynamicRBs[DynamicRBIndex];
		if (!RB.bIsKinematic)
		{
			// 刚体的有效逆质量 = 1/m + (r×n)ᵀ · I⁻¹ · (r×n)
			// Fix A: 用当前接触点计算力矩臂（Obi 风格，保持迭代间数值稳定）
			RBEffectiveW = RB.GetEffectiveInverseMass(GradientDir, CurrentContactPos);
		}
	}

	float TildeAlpha = Compliance / (DeltaTime * DeltaTime);
	float TotalW = ParticleW + RBEffectiveW + TildeAlpha;

	// ====== 计算 Lambda 增量 ======
	float dLambda;
	if (Evolution && Evolution->DampingMethod == EXPBDDampingMethod::Potential)
	{
		// Rayleigh dissipation potential damping
		float Gamma = Compliance * DampingBeta / DeltaTime;
		FVector dP = PredictPositions[ParticleIndex] - OriginalPositions[ParticleIndex];
		float SumVelocityGradient = GradientDir.Dot(dP);

		dLambda = (-C - TildeAlpha * Lambda - Gamma * SumVelocityGradient) /
			((1.0f + Gamma) * TotalW + SMALL_NUMBER);
	}
	else
	{
		// No damping
		dLambda = (-C - TildeAlpha * Lambda) / (TotalW + SMALL_NUMBER);
	}
	Lambda += dLambda;

	// ====== 应用位置校正到粒子 ======
	P += ParticleW * dLambda * GradientDir;

	// ====== 计算并累积冲量到刚体 ======
	if (DynamicRBIndex >= 0 && DynamicRBIndex < DynamicRBs.Num())
	{
		FDynamicRigidbodyProxy& RB = DynamicRBs[DynamicRBIndex];
		if (!RB.bIsKinematic)
		{
			// 冲量 = λ · n / Δt（反向施加给刚体）
			// 位置校正 Δx = λ · n，对应冲量 J = Δx / Δt = λ · n / Δt
			// Fix A: 用当前接触点施加冲量（角冲量力矩臂与有效质量计算点一致）
			const FVector Impulse = -dLambda * GradientDir / DeltaTime;
			RB.AccumulateImpulse(Impulse, CurrentContactPos);
		}
	}

	// ====== 旋转约束（Rod Pin 专用）======
	// 仅当 SegmentOrientationIndex 有效且提供了段方向数组时执行
	if (SegmentOrientationIndex != INDEX_NONE
		&& PredSegOrientations != nullptr && SegInvRotMass != nullptr
		&& PredSegOrientations->IsValidIndex(SegmentOrientationIndex)
		&& SegInvRotMass->IsValidIndex(SegmentOrientationIndex))
	{
		const FQuat& q_target = TargetOrientation;
		const FQuat  q_seg    = (*PredSegOrientations)[SegmentOrientationIndex];

		// Darboux 向量：q_target 与 q_seg 的相对旋转
		const FQuat  rel = q_target.Inverse() * q_seg;
		const FVector curr(rel.X, rel.Y, rel.Z);
		      FVector rest(RestDarboux.X, RestDarboux.Y, RestDarboux.Z);

		// 最短路径消歧（与 FXPBDBendTwistConstraint 相同）
		if (curr.Dot(rest) < 0.f)
		{
			rest = -rest;
		}

		const FVector ErrorRot     = curr - rest;
		const float   AlphaRot = RotationalCompliance / (DeltaTime * DeltaTime);
		const float   RotSumJacobiW     = (*SegInvRotMass)[SegmentOrientationIndex];
		const float   RotTotalSumJacobiW     = RotSumJacobiW + AlphaRot;

		if (RotTotalSumJacobiW > KINDA_SMALL_NUMBER)
		{
			// XPBD Lagrange 增量（三分量 component-wise）
			float dL[3];
			dL[0] = -(ErrorRot.X + AlphaRot * LambdaRot[0]) / RotTotalSumJacobiW;
			dL[1] = -(ErrorRot.Y + AlphaRot * LambdaRot[1]) / RotTotalSumJacobiW;
			dL[2] = -(ErrorRot.Z + AlphaRot * LambdaRot[2]) / RotTotalSumJacobiW;
			LambdaRot[0] += dL[0];
			LambdaRot[1] += dL[1];
			LambdaRot[2] += dL[2];

			if (RotSumJacobiW > 0.f)
			{
				// 旋转修正：q_seg += (q_target * dLPure) * w_seg ; normalize
				const FQuat dLPure(dL[0], dL[1], dL[2], 0.f);
				const FQuat q_target_dL = q_target * dLPure;
				FQuat& q_out = (*PredSegOrientations)[SegmentOrientationIndex];
				q_out = FQuat(
					q_seg.X + q_target_dL.X * RotSumJacobiW,
					q_seg.Y + q_target_dL.Y * RotSumJacobiW,
					q_seg.Z + q_target_dL.Z * RotSumJacobiW,
					q_seg.W + q_target_dL.W * RotSumJacobiW
				).GetNormalized();
			}
		}
	}
}
