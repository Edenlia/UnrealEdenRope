// Copyright Eden Games. All Rights Reserved.

#include "EdenRopeSolverActor.h"
#include "EdenRopeSettings.h"
#include "IEdenRopeEvolution.h"
#include "XPBDEdenRopeEvolution.h"
#include "Debug/EdenPhysicsShowFlags.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

#if WITH_EDITOR
#include "EdenRopeDebugRegistry.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#endif
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogEdenRopeSolver, Log, All);

// 静态空数组
const TArray<FVector> AEdenRopeSolverActor::EmptyVectorArray;
const TArray<FQuat>   AEdenRopeSolverActor::EmptyQuatArray;

// 静态生命周期事件
FOnEdenRopeSolverLifetime AEdenRopeSolverActor::OnEdenRopeSolverCreated;
FOnEdenRopeSolverLifetime AEdenRopeSolverActor::OnEdenRopeSolverDestroyed;

AEdenRopeSolverActor::AEdenRopeSolverActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	SetTickGroup(TG_PostPhysics);

	Evolution = MakeUnique<FXPBDEdenRopeEvolution>();
}

void AEdenRopeSolverActor::BeginPlay()
{
	Super::BeginPlay();

	// 从 Project Settings 读取配置
	SolverConfig = UEdenRopeSettings::Get()->SolverConfiguration;

	// 设置重力并应用配置
	if (Evolution && GetWorld())
	{
		// 重力单位转换：cm/s² → m/s²
		const FVector GravityCm = FVector(0.0f, 0.0f, GetWorld()->GetGravityZ());
		const FVector GravityM = GravityCm * 0.01f;
		Evolution->SetGravity(GravityM);
		
		// 设置 World 指针（用于 BVH 碰撞检测）
		Evolution->SetWorld(GetWorld());

		// 应用配置（摩擦、迭代次数等）
		ApplyConfig();

		UE_LOG(LogEdenRopeSolver, Log, TEXT("EdenRopeSolver BeginPlay: Using XPBD Evolution with BVH collision, Gravity = %s m/s², SolverIterations=%d"),
			*GravityM.ToString(),
			SolverConfig.SolverIterations);
	}

	// 广播 Solver 创建事件（供编辑器调试 Subsystem 等外部模块监听）
	OnEdenRopeSolverCreated.Broadcast(this);
}

void AEdenRopeSolverActor::ApplyConfig()
{
	if (!Evolution)
	{
		return;
	}

	// 注意：由于 UE 禁用 RTTI，使用 static_cast（当前硬编码为 XPBD）
	FXPBDEdenRopeEvolution* XPBDEvolution = static_cast<FXPBDEdenRopeEvolution*>(Evolution.Get());

	// Segment 碰撞全局参数（SelfCollisionMargin 仍为全局）
	XPBDEvolution->SelfCollisionMargin = SolverConfig.SelfCollisionMargin;

	// 全局速度阻尼
	XPBDEvolution->GlobalDamping = SolverConfig.GlobalDamping;

	Evolution->SetSolverIterations(SolverConfig.SolverIterations);
}

void AEdenRopeSolverActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 在 Evolution 释放前广播销毁事件，订阅方可安全解除对本 Solver 的引用
	OnEdenRopeSolverDestroyed.Broadcast(this);

	Evolution.Reset();
	Super::EndPlay(EndPlayReason);
}

void AEdenRopeSolverActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!Evolution)
	{
		return;
	}

	// 只有 bAutoSimulate 开启时才在 Tick 中执行模拟
	if (bAutoSimulate)
	{
		// 钳制 DeltaTime，防止失去焦点后恢复时物理爆炸
		const float ClampedDeltaTime = FMath::Min(DeltaTime, SolverConfig.MaxPhysicsDeltaTime);

		if (!SolverConfig.bUseFixedTimestep)
		{
			// === Variable timestep ===
			InterpolationAlpha = 0.0f;
			Evolution->Simulate(ClampedDeltaTime);
		}
		else
		{
			// === Fixed timestep + accumulation ===
			AccumulatedTime += ClampedDeltaTime;

			int32 NumSteps = FMath::FloorToInt32(AccumulatedTime / SolverConfig.FixedDeltaTime);

			// Spiral-of-death 保护：限制最大子步数，超出时间丢弃
			if (NumSteps > SolverConfig.MaxSubsteps)
			{
				NumSteps = SolverConfig.MaxSubsteps;
			}

			// 只保留一个 FixedDeltaTime 以内的余量（超出部分丢弃，防止累积器无限增长）
			AccumulatedTime = FMath::Fmod(AccumulatedTime, SolverConfig.FixedDeltaTime);

			// 计算插值 alpha（剩余时间占一个固定步长的比例）
			InterpolationAlpha = AccumulatedTime / SolverConfig.FixedDeltaTime;

			// 执行 N 次固定步长物理步
			for (int32 Step = 0; Step < NumSteps; ++Step)
			{
				Evolution->Simulate(SolverConfig.FixedDeltaTime);
			}
		}
	}

	// DebugDraw 每帧都执行，与模拟模式无关
	DoDebugDraw();
}

void AEdenRopeSolverActor::SimulateStep(float DeltaTime)
{
	if (Evolution)
	{
		Evolution->Simulate(DeltaTime);
	}
}

void AEdenRopeSolverActor::DebugSimulateOneStep(float DeltaTime)
{
	SimulateStep(DeltaTime);
}

void AEdenRopeSolverActor::DoDebugDraw()
{
#if WITH_EDITORONLY_DATA
	if (!Evolution)
	{
		return;
	}

	// 参考 Navigation 系统的 IsNavigationShowFlagSet 实现
	// 获取任一viewport启用的ShowFlags
	bool bDrawParticle = false;
	bool bDrawMaterialFrameAxis = false;
	bool bDrawPin = false;
	bool bDrawDistance = false;
	bool bDrawBend = false;
	bool bDrawRBScene = false;
	bool bDrawRopeAABB = false;
	bool bDrawCollisionConstraints = false;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FWorldContext* WorldContext = GEngine ? GEngine->GetWorldContextFromWorld(World) : nullptr;

#if WITH_EDITOR
	if (GEditor && WorldContext && WorldContext->WorldType != EWorldType::Game)
	{
		// Editor模式（非Game）：先检查GameViewport
		if (WorldContext->GameViewport)
		{
			const FEngineShowFlags& ShowFlags = WorldContext->GameViewport->EngineShowFlags;
			bDrawParticle = GShowEdenPhysicsParticles.IsEnabled(ShowFlags);
			bDrawMaterialFrameAxis = GShowEdenMaterialFrameAxis.IsEnabled(ShowFlags);
			bDrawPin = GShowEdenPhysicsPinConstraint.IsEnabled(ShowFlags);
			bDrawDistance = GShowEdenPhysicsDistanceConstraint.IsEnabled(ShowFlags);
			bDrawBend = GShowEdenPhysicsBendConstraint.IsEnabled(ShowFlags);
			bDrawRopeAABB = GShowEdenPhysicsRopeAABB.IsEnabled(ShowFlags);
			bDrawCollisionConstraints = GShowEdenPhysicsCollisionConstraints.IsEnabled(ShowFlags);
		}

		// 如果GameViewport没有启用，遍历所有Editor viewport
		if (!bDrawParticle && !bDrawMaterialFrameAxis && !bDrawPin && !bDrawDistance && !bDrawBend && !bDrawRBScene && !bDrawRopeAABB && !bDrawCollisionConstraints)
		{
			for (FEditorViewportClient* ViewportClient : GEditor->GetAllViewportClients())
			{
				if (ViewportClient)
				{
					const FEngineShowFlags& ShowFlags = ViewportClient->EngineShowFlags;
					bDrawParticle |= GShowEdenPhysicsParticles.IsEnabled(ShowFlags);
					bDrawMaterialFrameAxis |= GShowEdenMaterialFrameAxis.IsEnabled(ShowFlags);
					bDrawPin |= GShowEdenPhysicsPinConstraint.IsEnabled(ShowFlags);
					bDrawDistance |= GShowEdenPhysicsDistanceConstraint.IsEnabled(ShowFlags);
					bDrawBend |= GShowEdenPhysicsBendConstraint.IsEnabled(ShowFlags);
					bDrawRopeAABB |= GShowEdenPhysicsRopeAABB.IsEnabled(ShowFlags);
					bDrawCollisionConstraints |= GShowEdenPhysicsCollisionConstraints.IsEnabled(ShowFlags);
				}
			}
		}
	}
	else
#endif // WITH_EDITOR
	{
		// Game模式：从GameViewport获取ShowFlags
		if (WorldContext && WorldContext->GameViewport)
		{
			const FEngineShowFlags& ShowFlags = WorldContext->GameViewport->EngineShowFlags;
			bDrawParticle = GShowEdenPhysicsParticles.IsEnabled(ShowFlags);
			bDrawMaterialFrameAxis = GShowEdenMaterialFrameAxis.IsEnabled(ShowFlags);
			bDrawPin = GShowEdenPhysicsPinConstraint.IsEnabled(ShowFlags);
			bDrawDistance = GShowEdenPhysicsDistanceConstraint.IsEnabled(ShowFlags);
			bDrawBend = GShowEdenPhysicsBendConstraint.IsEnabled(ShowFlags);
			bDrawRopeAABB = GShowEdenPhysicsRopeAABB.IsEnabled(ShowFlags);
			bDrawCollisionConstraints = GShowEdenPhysicsCollisionConstraints.IsEnabled(ShowFlags);
		}
	}

	FEdenRopeDebugDrawParams DebugParams;
	DebugParams.bDrawParticle = bDrawParticle;
	DebugParams.bDrawMaterialFrameAxis = bDrawMaterialFrameAxis;
	DebugParams.bDrawPin = bDrawPin;
	DebugParams.bDrawDistance = bDrawDistance;
	DebugParams.bDrawBend = bDrawBend;
	DebugParams.bDrawRBScene = bDrawRBScene;
	DebugParams.bDrawRopeAABB = bDrawRopeAABB;
	DebugParams.bDrawCollisionConstraints = bDrawCollisionConstraints;
	Evolution->DebugDraw(World, DebugParams);
#endif
}

FEdenRopeHandle AEdenRopeSolverActor::RegisterRope(
	const FVector& StartPositionCm,
	const FVector& Direction,
	const FEdenRopeCreationParams& Params)
{
	if (Evolution)
	{
		return Evolution->RegisterRope(StartPositionCm, Direction, Params);
	}

	UE_LOG(LogEdenRopeSolver, Warning, TEXT("RegisterRope: Evolution is null"));
	return FEdenRopeHandle();
}

FEdenRopeHandle AEdenRopeSolverActor::RegisterRope(
	TArrayView<const FVector> InitialPositionsCm,
	const FEdenRopeCreationParams& Params)
{
	if (Evolution)
	{
		return Evolution->RegisterRope(InitialPositionsCm, Params);
	}

	UE_LOG(LogEdenRopeSolver, Warning, TEXT("RegisterRope(positions): Evolution is null"));
	return FEdenRopeHandle();
}

FEdenRopeHandle AEdenRopeSolverActor::RegisterRod(
	const FVector& StartPositionCm,
	const FVector& Direction,
	const FEdenRopeCreationParams& Params)
{
	if (Evolution)
	{
		return Evolution->RegisterRod(StartPositionCm, Direction, Params);
	}

	UE_LOG(LogEdenRopeSolver, Warning, TEXT("RegisterRod: Evolution is null"));
	return FEdenRopeHandle();
}

FEdenRopeHandle AEdenRopeSolverActor::RegisterRod(
	TArrayView<const FVector> InitialPositionsCm,
	TArrayView<const FQuat> InitialOrientations,
	const FEdenRopeCreationParams& Params)
{
	if (Evolution)
	{
		return Evolution->RegisterRod(InitialPositionsCm, InitialOrientations, Params);
	}

	UE_LOG(LogEdenRopeSolver, Warning, TEXT("RegisterRod(positions): Evolution is null"));
	return FEdenRopeHandle();
}

void AEdenRopeSolverActor::UnregisterRope(FEdenRopeHandle& Handle)
{
	if (Evolution)
	{
		Evolution->UnregisterRope(Handle);
	}
}

FEdenPinConstraintHandle AEdenRopeSolverActor::RegisterPinConstraint(
	uint32 ParticleGlobalIndex,
	const FVector& PositionCm)
{
	if (Evolution)
	{
		return Evolution->RegisterPinConstraint(ParticleGlobalIndex, PositionCm);
	}

	UE_LOG(LogEdenRopeSolver, Warning, TEXT("RegisterPinConstraint: Evolution is null"));
	return FEdenPinConstraintHandle();
}

FEdenPinConstraintHandle AEdenRopeSolverActor::RegisterPinConstraintToComponent(
	uint32 ParticleGlobalIndex,
	UPrimitiveComponent* Component,
	const FVector& LocalOffsetCm)
{
	if (Evolution)
	{
		return Evolution->RegisterPinConstraintToComponent(ParticleGlobalIndex, Component, LocalOffsetCm);
	}

	UE_LOG(LogEdenRopeSolver, Warning, TEXT("RegisterPinConstraintToComponent: Evolution is null"));
	return FEdenPinConstraintHandle();
}

FEdenPinConstraintHandle AEdenRopeSolverActor::RegisterPinConstraintWithOrientation(
	uint32 ParticleGlobalIndex,
	const FVector& PositionCm,
	int32 SegmentOrientationIndex,
	const FQuat& TargetOrientation,
	const FQuat& RestDarboux)
{
	if (Evolution)
	{
		return Evolution->RegisterPinConstraintWithOrientation(
			ParticleGlobalIndex, PositionCm, SegmentOrientationIndex, TargetOrientation, RestDarboux);
	}
	return FEdenPinConstraintHandle();
}

void AEdenRopeSolverActor::UpdatePinConstraintPosition(
	const FEdenPinConstraintHandle& Handle,
	const FVector& NewPositionCm)
{
	if (Evolution)
	{
		Evolution->UpdatePinConstraintPosition(Handle, NewPositionCm);
	}
}

void AEdenRopeSolverActor::UpdatePinConstraintOrientation(
	const FEdenPinConstraintHandle& Handle,
	const FQuat& NewTargetOrientation)
{
	if (Evolution)
	{
		Evolution->UpdatePinConstraintOrientation(Handle, NewTargetOrientation);
	}
}

const TArray<FVector>& AEdenRopeSolverActor::GetParticlePositions() const
{
	if (Evolution)
	{
		return Evolution->GetParticlePositions();
	}
	return EmptyVectorArray;
}

const TArray<FVector>& AEdenRopeSolverActor::GetParticleVelocities() const
{
	if (Evolution)
	{
		return Evolution->GetParticleVelocities();
	}
	return EmptyVectorArray;
}

const TArray<FVector>& AEdenRopeSolverActor::GetPreviousParticlePositions() const
{
	if (Evolution)
	{
		return Evolution->GetPreviousParticlePositions();
	}
	return EmptyVectorArray;
}

void AEdenRopeSolverActor::SetSolverIterations(uint32 Iterations)
{
	if (Evolution)
	{
		Evolution->SetSolverIterations(Iterations);
	}
}

void AEdenRopeSolverActor::SetRopeAllParticleMass(const FEdenRopeHandle& Handle, float NewMass)
{
	if (Evolution)
	{
		Evolution->SetAllParticleMass(Handle, NewMass);
	}
}

void AEdenRopeSolverActor::SetSingleParticleMass(const FEdenRopeHandle& Handle, int32 LocalIndex, float NewMass)
{
	if (Evolution)
	{
		Evolution->SetSingleParticleMass(Handle, LocalIndex, NewMass);
	}
}

float AEdenRopeSolverActor::GetSingleParticleMass(const FEdenRopeHandle& Handle, int32 LocalIndex) const
{
	if (Evolution)
	{
		return Evolution->GetSingleParticleMass(Handle, LocalIndex);
	}
	return -1.f;
}

void AEdenRopeSolverActor::SetRodAllSegmentRotationalMass(const FEdenRopeHandle& Handle, float InertiaPerUnitLength, float LengthCm)
{
	if (Evolution)
	{
		Evolution->SetAllSegmentRotationalMass(Handle, InertiaPerUnitLength, LengthCm);
	}
}

const TArray<FQuat>& AEdenRopeSolverActor::GetSegmentOrientations() const
{
	if (Evolution)
	{
		return Evolution->GetSegmentOrientations();
	}
	return EmptyQuatArray;
}

const TArray<FQuat>& AEdenRopeSolverActor::GetPreviousSegmentOrientations() const
{
	if (Evolution)
	{
		return Evolution->GetPreviousSegmentOrientations();
	}
	return EmptyQuatArray;
}

#if WITH_EDITOR
void AEdenRopeSolverActor::EditorNotifyRopeRegistered(const FEdenRopeHandle& Handle, const FString& InDisplayName)
{
	if (Evolution)
	{
		Evolution->EditorNotifyRopeRegistered(Handle, InDisplayName);
	}
}

FEdenRopeDebugRegistry* AEdenRopeSolverActor::GetEvolutionDebugRegistry() const
{
	if (Evolution)
	{
		return &Evolution->GetDebugRegistry();
	}
	return nullptr;
}
#endif

// Note: CollectStaticColliders and ProcessGeometry have been removed.
// Collision detection now uses the Chaos BVH directly via FEdenRopeOverlapVisitor.
