// Copyright Eden Games. All Rights Reserved.

#include "Components/EdenRodComponent.h"
#include "Macro/EdenRopeMacroDefine.h"
#include "Solver/EdenRopeSolverActor.h"
#include "Solver/EdenRopeWorldSubsystem.h"

UEdenRodComponent::UEdenRodComponent()
{
	// Base class sets up tick; nothing Rod-specific needed here
}

void UEdenRodComponent::RegisterToSolver()
{
	UEdenRopeWorldSubsystem* Subsystem = UEdenRopeWorldSubsystem::Get(GetWorld());
	if (!Subsystem) return;

	AEdenRopeSolverActor* Solver = Subsystem->GetSolver();
	if (!Solver) return;

	CachedSolver = Solver;

	SetTickGroup(TG_PostPhysics);
	PrimaryComponentTick.AddPrerequisite(Solver, Solver->PrimaryActorTick);

	FEdenRopeCreationParams Params;
	Params.NumParticles                  = NumParticles;
	Params.LengthCm                      = Length;
	Params.MassPerParticle               = MassPerParticle;
	Params.ParticleRadiusCm              = ParticleRadius;
	Params.StretchCompliance             = StretchCompliance;
	Params.Shear1Compliance              = Shear1Compliance;
	Params.Shear2Compliance              = Shear2Compliance;
	Params.TwistCompliance               = TwistCompliance;
	Params.Bend1Compliance               = Bend1Compliance;
	Params.Bend2Compliance               = Bend2Compliance;
	Params.SegmentInertiaPerUnitLength    = SegmentInertiaPerUnitLength;
	Params.bKeepInitialShape             = bKeepInitialShape;

	// Per-Component Friction & Self Collision
	Params.GroundFrictionProportional    = GroundFrictionProportional;
	Params.GroundFrictionConstant        = GroundFrictionConstant;
	Params.bEnableSelfCollision          = bEnableSelfCollision;
	Params.SelfCollisionSkipCount        = SelfCollisionSkipCount;

	// 将世界缩放存入 Params，保持 LengthCm / ParticleRadiusCm 等为原始局部空间值
	// Solver 侧在创建粒子和约束时统一应用 WorldScale3D
	Params.WorldScale3D = GetComponentTransform().GetScale3D();

	// Try spline-based initialization first (directly from persistent AuthoringCurves)
	if (AuthoringCurves.Position.Points.Num() >= 2)
	{
		TArray<FVector> SampledPositions;
		TArray<FQuat> SampledOrientations;
		if (SampleFromCurves(AuthoringCurves, NumParticles, Length, SampledPositions, &SampledOrientations))
		{
			RopeHandle = Solver->RegisterRod(
				TArrayView<const FVector>(SampledPositions),
				TArrayView<const FQuat>(SampledOrientations),
				Params);
		}
	}

	// Fallback to linear initialization
	if (!RopeHandle.IsValid())
	{
		const FVector Direction = GetComponentQuat().RotateVector(FVector(1.0f, 0.0f, 0.0f));
		RopeHandle = Solver->RegisterRod(GetComponentLocation(), Direction, Params);
	}

	if (!RopeHandle.IsValid())
	{
		UE_LOG(LogEdenRope, Warning, TEXT("UEdenRodComponent::RegisterToSolver: Failed to register rod"));
		return;
	}

	UE_LOG(LogEdenRope, Log,
		TEXT("UEdenRodComponent::RegisterToSolver: OK (ParticleStart=%d, Count=%d, SegOrientStart=%d, SegCount=%d)"),
		RopeHandle.ParticleStartIndex, RopeHandle.ParticleCount,
		RopeHandle.SegmentOrientationStartIndex, RopeHandle.SegmentOrientationCount);

	bSolverRegistered = true;

#if WITH_EDITOR
	{
		const FString DisplayName = GetReadableName();
		Solver->EditorNotifyRopeRegistered(RopeHandle, DisplayName);
	}
#endif

	OnRopeRegistered.Broadcast(RopeHandle);
}

void UEdenRodComponent::SetSegmentInertiaPerUnitLength(float NewInertia)
{
	if (NewInertia <= 0.f)
		return;

	SegmentInertiaPerUnitLength = NewInertia;

	if (IsSolverRegistered() && GetRopeHandle().IsValid())
	{
		if (AEdenRopeSolverActor* Solver = CachedSolver.Get())
		{
			Solver->SetRodAllSegmentRotationalMass(GetRopeHandle(), NewInertia, Length);
		}
	}
}

void UEdenRodComponent::UnregisterFromSolver()
{
	if (!RopeHandle.IsValid())
		return;

	if (AEdenRopeSolverActor* Solver = CachedSolver.Get())
	{
		Solver->UnregisterRope(RopeHandle);
	}

	RopeHandle.Reset();
	CachedSolver.Reset();
	bSolverRegistered = false;
}

void UEdenRodComponent::SyncFromSolver()
{
	if (!RopeHandle.IsValid())
		return;

	AEdenRopeSolverActor* Solver = CachedSolver.Get();
	if (!Solver)
		return;

	const int32 Count      = RopeHandle.ParticleCount;
	const int32 StartIndex = RopeHandle.ParticleStartIndex;
	const int32 SegStart   = RopeHandle.SegmentOrientationStartIndex;
	const int32 SegCount   = RopeHandle.SegmentOrientationCount;

	// ===== 位置同步（与 Rope 相同）=====
	const TArray<FVector>& CurrentPositionsM = Solver->GetParticlePositions();
	CachedParticlePositions.Reset(Count);

	if (Solver->IsFixedTimestepEnabled())
	{
		const TArray<FVector>& PrevPositionsM = Solver->GetPreviousParticlePositions();
		const float Alpha = Solver->GetInterpolationAlpha();

		for (int32 i = 0; i < Count; ++i)
		{
			const int32 Idx = StartIndex + i;
			if (CurrentPositionsM.IsValidIndex(Idx) && PrevPositionsM.IsValidIndex(Idx))
			{
				const FVector InterpM = FMath::Lerp(PrevPositionsM[Idx], CurrentPositionsM[Idx], Alpha);
				CachedParticlePositions.Add(InterpM * 100.0f);  // m -> cm
			}
		}
	}
	else
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 Idx = StartIndex + i;
			if (CurrentPositionsM.IsValidIndex(Idx))
			{
				CachedParticlePositions.Add(CurrentPositionsM[Idx] * 100.0f);  // m -> cm
			}
		}
	}

	// ===== 方向同步（Rod 独有）=====
	// 粒子 i 使用段方向 q[min(i, SegCount-1)]（最后粒子复用最后一段方向）
	const TArray<FQuat>& SegsNow  = Solver->GetSegmentOrientations();
	const TArray<FQuat>& SegsPrev = Solver->GetPreviousSegmentOrientations();

	CachedParticleOrientations.Reset(Count);

	if (SegStart == INDEX_NONE || SegCount <= 0)
	{
		// 没有段方向数据，留空（退化为位置模式）
		return;
	}

	const bool  bFixed = Solver->IsFixedTimestepEnabled();
	const float Alpha  = Solver->GetInterpolationAlpha();

	for (int32 i = 0; i < Count; ++i)
	{
		// 最后粒子复用最后一段方向
		const int32 SegIdx = SegStart + FMath::Min(i, SegCount - 1);

		FQuat Q;
		if (bFixed && SegsPrev.IsValidIndex(SegIdx) && SegsNow.IsValidIndex(SegIdx))
		{
			Q = FQuat::Slerp(SegsPrev[SegIdx], SegsNow[SegIdx], Alpha);
		}
		else if (SegsNow.IsValidIndex(SegIdx))
		{
			Q = SegsNow[SegIdx];
		}
		else
		{
			Q = FQuat::Identity;
		}

		// 世界空间方向（SendRenderDynamicData_Concurrent 会转换到组件空间）
		CachedParticleOrientations.Add(Q);
	}

	MarkRenderDynamicDataDirty();
	UpdateComponentToWorld();
}
