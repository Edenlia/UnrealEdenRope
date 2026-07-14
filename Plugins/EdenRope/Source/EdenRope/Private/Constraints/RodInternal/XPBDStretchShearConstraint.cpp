// Copyright Eden Games. All Rights Reserved.

#include "XPBDStretchShearConstraint.h"
#include "Macro/EdenRopeMacroDefine.h"

void FXPBDStretchShearConstraint::Project(
	const TArray<FVector>& OrigPos, TArray<FVector>& PredPos,
	const TArray<float>&   InvMass,
	const TArray<FQuat>&   OrigOrient, TArray<FQuat>& PredOrient,
	const TArray<float>&   InvRotMass, float Dt)
{
	const int32 P1  = (int32)ParticleIndex1;
	const int32 P2  = (int32)ParticleIndex2;
	const int32 Seg = (int32)SegmentIndex;

	if (!PredPos.IsValidIndex(P1) || !PredPos.IsValidIndex(P2) || !PredOrient.IsValidIndex(Seg))
		return;

	if (RestLength <= KINDA_SMALL_NUMBER)
		return;

	// e_3 = (0, 0, 1) 是静止状态下切向（材质 Z 轴）
	const FQuat&  qPred = PredOrient[Seg];
	const FVector dPos    = PredPos[P2] - PredPos[P1];

	// TODO: After verify world space implementation, Impl material frame projection
#if ROD_MATERIAL_FRAME_CONSTRAINT_PROJECTION
	const FVector C = qPred.Inverse().RotateVector(dPos) / RestLength - FVector(0.f, 0.f, 1.f);
#else
	const FVector C = dPos / RestLength - qPred.RotateVector(FVector(0.f, 0.f, 1.f));
#endif
	
	// XPBD tilde-compliance（per component）
	const FVector alpha = Compliance / (Dt * Dt);

	// In World Space
	// dCdP1 = -1 / L
	// dCdP2 =  1 / L
	// W_P1	 = w_p1
	// W_P2  = w_p2
	// W_Q   = 4 * w_q0

	const float GeomSumJacobiW = ((InvMass[P1] + InvMass[P2]) / (RestLength * RestLength) + 4 * InvRotMass[Seg]);

	const FVector TotalSumJacobiW = FVector(GeomSumJacobiW) + alpha;

	FVector dL;
	dL.X = (TotalSumJacobiW.X > KINDA_SMALL_NUMBER) ? -(C.X + alpha.X * Lambda.X) / TotalSumJacobiW.X : 0.f;
	dL.Y = (TotalSumJacobiW.Y > KINDA_SMALL_NUMBER) ? -(C.Y + alpha.Y * Lambda.Y) / TotalSumJacobiW.Y : 0.f;
	dL.Z = (TotalSumJacobiW.Z > KINDA_SMALL_NUMBER) ? -(C.Z + alpha.Z * Lambda.Z) / TotalSumJacobiW.Z : 0.f;
	Lambda += dL;
#if ROD_MATERIAL_FRAME_CONSTRAINT_PROJECTION
	// [Material Frame] -> [World Space]
	dL = qPred.RotateVector(dL);
#endif
	if (InvMass[P1] > 0.f)
		PredPos[P1] += -(InvMass[P1] / RestLength) * dL;
	if (InvMass[P2] > 0.f)
		PredPos[P2] += +(InvMass[P2] / RestLength) * dL;
	if (InvRotMass[Seg] > 0.f)
	{
		const FQuat dL_Quat = FQuat(dL.X, dL.Y, dL.Z, 0.f);
		const FQuat qe3          = qPred * FQuat(0.f, 0.f, -1.f, 0.f); // q\bar{e}_3
		const FQuat rotDelta     = dL_Quat * qe3;
		// J_q^T x = -2 * Quat(x) * q * ē₃, so scale = -2 * w_q (factor 2 from Jacobian)
		const float scale        = -2.f * InvRotMass[Seg];
		PredOrient[Seg] = FQuat(
			qPred.X + rotDelta.X * scale,
			qPred.Y + rotDelta.Y * scale,
			qPred.Z + rotDelta.Z * scale,
			qPred.W + rotDelta.W * scale
		).GetNormalized();
	}
}
