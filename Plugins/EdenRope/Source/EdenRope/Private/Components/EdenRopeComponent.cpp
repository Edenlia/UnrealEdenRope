// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/EdenRopeComponent.h"
#include "Macro/EdenRopeMacroDefine.h"
#include "Solver/EdenRopeSolverActor.h"
#include "Solver/EdenRopeWorldSubsystem.h"

UEdenRopeComponent::UEdenRopeComponent()
{
	// Base class sets up tick; nothing Rope-specific needed here
}

void UEdenRopeComponent::RegisterToSolver()
{
	UEdenRopeWorldSubsystem* Subsystem = UEdenRopeWorldSubsystem::Get(GetWorld());
	if (!Subsystem) return;

	AEdenRopeSolverActor* Solver = Subsystem->GetSolver();
	if (!Solver) return;

	CachedSolver = Solver;

	SetTickGroup(TG_PostPhysics);
	PrimaryComponentTick.AddPrerequisite(Solver, Solver->PrimaryActorTick);

	FEdenRopeCreationParams Params;
	Params.NumParticles                = NumParticles;
	Params.LengthCm                    = Length;
	Params.MassPerParticle             = MassPerParticle;
	Params.DistanceStiffness           = DistanceStiffness;
	Params.DistanceCompressThresholdCm = DistanceCompressThreshold;
	Params.BendStiffness               = BendStiffness;
	Params.MinBendAngleCoefficient     = MinBendAngleCoefficient;
	Params.ParticleRadiusCm            = ParticleRadius;

	// Per-Component Friction & Self Collision
	Params.GroundFrictionProportional  = GroundFrictionProportional;
	Params.GroundFrictionConstant      = GroundFrictionConstant;
	Params.bEnableSelfCollision        = bEnableSelfCollision;
	Params.SelfCollisionSkipCount      = SelfCollisionSkipCount;
	
	// 将世界缩放存入 Params，保持 LengthCm / ParticleRadiusCm 等为原始局部空间值
	// Solver 侧在创建粒子和约束时统一应用 WorldScale3D
	Params.WorldScale3D = GetComponentTransform().GetScale3D();

	// Try spline-based initialization first (directly from persistent AuthoringCurves)
	if (AuthoringCurves.Position.Points.Num() >= 2)
	{
		TArray<FVector> SampledPositions;
		if (SampleFromCurves(AuthoringCurves, NumParticles, Length, SampledPositions))
		{
			RopeHandle = Solver->RegisterRope(TArrayView<const FVector>(SampledPositions), Params);
		}
	}

	// Fallback to linear initialization
	if (!RopeHandle.IsValid())
	{
		const FVector Direction = GetComponentQuat().RotateVector(FVector(1.0f, 0.0f, 0.0f));
		RopeHandle = Solver->RegisterRope(GetComponentLocation(), Direction, Params);
	}

	if (!RopeHandle.IsValid())
	{
		UE_LOG(LogEdenRope, Warning, TEXT("UEdenRopeComponent::RegisterToSolver: Failed to register rope"));
		return;
	}

	UE_LOG(LogEdenRope, Log, TEXT("UEdenRopeComponent::RegisterToSolver: OK (ParticleStart=%d, Count=%d)"),
		RopeHandle.ParticleStartIndex, RopeHandle.ParticleCount);

	bSolverRegistered = true;

#if WITH_EDITOR
	{
		const FString DisplayName = GetReadableName();
		Solver->EditorNotifyRopeRegistered(RopeHandle, DisplayName);
	}
#endif

	OnRopeRegistered.Broadcast(RopeHandle);
}

void UEdenRopeComponent::UnregisterFromSolver()
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

void UEdenRopeComponent::SyncFromSolver()
{
	if (!RopeHandle.IsValid())
		return;

	AEdenRopeSolverActor* Solver = CachedSolver.Get();
	if (!Solver)
		return;

	const int32 StartIndex = RopeHandle.ParticleStartIndex;
	const int32 Count      = RopeHandle.ParticleCount;
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

	// Rope 不使用方向（纯位置模拟）
	CachedParticleOrientations.Empty();

	MarkRenderDynamicDataDirty();
	UpdateComponentToWorld();
}
