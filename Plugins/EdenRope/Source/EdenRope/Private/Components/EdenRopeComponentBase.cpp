// Copyright Eden Games. All Rights Reserved.

#include "Components/EdenRopeComponentBase.h"
#include "Macro/EdenRopeMacroDefine.h"
#include "Render/EdenRopeSceneProxy.h"
#include "Solver/EdenRopeSolverActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

UEdenRopeComponentBase::UEdenRopeComponentBase()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetTickGroup(TG_PostPhysics);
	bTickInEditor = true;

	// Initialize AuthoringCurves with two default points along local X axis
	// matching the rope's default direction (origin → (100, 0, 0))
	AuthoringCurves.Position.Points.Reset(2);
	AuthoringCurves.Rotation.Points.Reset(2);
	AuthoringCurves.Scale.Points.Reset(2);

	AuthoringCurves.Position.Points.Emplace(0.0f, FVector(0, 0, 0), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);
	AuthoringCurves.Rotation.Points.Emplace(0.0f, FQuat::Identity, FQuat::Identity, FQuat::Identity, CIM_CurveAuto);
	AuthoringCurves.Scale.Points.Emplace(0.0f, FVector(1.0f), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);

	AuthoringCurves.Position.Points.Emplace(1.0f, FVector(100, 0, 0), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);
	AuthoringCurves.Rotation.Points.Emplace(1.0f, FQuat::Identity, FQuat::Identity, FQuat::Identity, CIM_CurveAuto);
	AuthoringCurves.Scale.Points.Emplace(1.0f, FVector(1.0f), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);

	AuthoringCurves.UpdateSpline();
}

void UEdenRopeComponentBase::OnRegister()
{
	Super::OnRegister();

	InitializeDefaultParticlePositions();

	if (RenderMode == ERopeRenderMode::SegmentMesh)
		CreateSegmentMeshISMC();
}

void UEdenRopeComponentBase::OnUnregister()
{
	DestroySegmentMeshISMC();
	Super::OnUnregister();
}

void UEdenRopeComponentBase::InitializeDefaultParticlePositions()
{
	// Try to sample from AuthoringCurves first (gives spline-shaped preview)
	if (AuthoringCurves.Position.Points.Num() >= 2 && NumParticles >= 2 && Length > 0.f)
	{
		TArray<FVector> SampledPositions;
		if (SampleFromCurves(AuthoringCurves, NumParticles, Length, SampledPositions))
		{
			CachedParticlePositions = MoveTemp(SampledPositions);
			return;
		}
	}

	// Fallback: straight line from start to end
	const FVector StartPos = GetComponentLocation();
	const FVector EndPos   = GetComponentTransform().TransformPosition(FVector(Length, 0, 0));
	const FVector Delta    = EndPos - StartPos;

	CachedParticlePositions.Reset(NumParticles);
	for (int32 i = 0; i < NumParticles; ++i)
	{
		const float Alpha = static_cast<float>(i) / FMath::Max(1, NumParticles - 1);
		CachedParticlePositions.Add(StartPos + Alpha * Delta);
	}
}

FPrimitiveSceneProxy* UEdenRopeComponentBase::CreateSceneProxy()
{
	if (RenderMode == ERopeRenderMode::SegmentMesh)
		return nullptr;
	return new FEdenRopeSceneProxy(this);
}

int32 UEdenRopeComponentBase::GetNumMaterials() const
{
	return 1;
}

FBoxSphereBounds UEdenRopeComponentBase::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox RopeBox(ForceInit);

	const FTransform& ComponentTransform = GetComponentTransform();
	for (const FVector& Position : CachedParticlePositions)
	{
		RopeBox += ComponentTransform.InverseTransformPosition(Position);
	}

	if (!RopeBox.IsValid)
	{
		RopeBox = FBox(FVector::ZeroVector, FVector(Length, 0, 0));
	}

	return FBoxSphereBounds(RopeBox.ExpandBy(0.5f * CableWidth)).TransformBy(LocalToWorld);
}

void UEdenRopeComponentBase::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);
	FRegisterComponentContext::SendRenderDynamicData(Context, this);
}

void UEdenRopeComponentBase::SendRenderDynamicData_Concurrent()
{
	if (RenderMode == ERopeRenderMode::SegmentMesh)
		return;

	if (SceneProxy)
	{
		FEdenRopeRenderDynamicData* DynamicData = new FEdenRopeRenderDynamicData;

		const FTransform& ComponentTransform = GetComponentTransform();
		const int32 NumPoints = CachedParticlePositions.Num();
		DynamicData->RopePoints.Reserve(NumPoints);
		for (const FVector& WorldPos : CachedParticlePositions)
		{
			DynamicData->RopePoints.Add(ComponentTransform.InverseTransformPosition(WorldPos));
		}

		if (CachedParticleOrientations.Num() == NumPoints)
		{
			const FQuat InvCompRotation = ComponentTransform.GetRotation().Inverse();
			DynamicData->RopeOrientations.Reserve(NumPoints);
			for (const FQuat& WorldRot : CachedParticleOrientations)
			{
				DynamicData->RopeOrientations.Add(InvCompRotation * WorldRot);
			}
		}

		FEdenRopeSceneProxy* RopeSceneProxy = static_cast<FEdenRopeSceneProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(FSendRopeDynamicData)(UE::RenderCommandPipe::EdenRope,
			[RopeSceneProxy, DynamicData](FRHICommandListBase& RHICmdList)
		{
			RopeSceneProxy->SetDynamicData_RenderThread(RHICmdList, DynamicData);
		});
	}
}

void UEdenRopeComponentBase::GetRopeParticleLocations(TArray<FVector>& OutLocations) const
{
	OutLocations = CachedParticlePositions;
}

FVector UEdenRopeComponentBase::GetRopeStartWorldLocation() const
{
	if (CachedParticlePositions.Num() > 0)
	{
		return CachedParticlePositions[0];
	}
	return GetComponentLocation();
}

FVector UEdenRopeComponentBase::GetRopeEndWorldLocation() const
{
	if (CachedParticlePositions.Num() > 0)
	{
		return CachedParticlePositions.Last();
	}
	return GetComponentLocation();
}

void UEdenRopeComponentBase::SetMassPerParticle(float NewMass)
{
	if (NewMass <= 0.f)
		return;

	MassPerParticle = NewMass;

	if (bSolverRegistered && RopeHandle.IsValid())
	{
		if (AEdenRopeSolverActor* Solver = CachedSolver.Get())
		{
			Solver->SetRopeAllParticleMass(RopeHandle, NewMass);
		}
	}
}

void UEdenRopeComponentBase::SetSingleParticleMass(int32 LocalIndex, float NewMass)
{
	if (NewMass <= 0.f || LocalIndex < 0 || LocalIndex >= NumParticles)
		return;

	if (bSolverRegistered && RopeHandle.IsValid())
	{
		if (AEdenRopeSolverActor* Solver = CachedSolver.Get())
		{
			Solver->SetSingleParticleMass(RopeHandle, LocalIndex, NewMass);
		}
	}
}

float UEdenRopeComponentBase::GetSingleParticleMass(int32 LocalIndex) const
{
	if (LocalIndex < 0 || LocalIndex >= NumParticles)
		return -1.f;

	if (bSolverRegistered && RopeHandle.IsValid())
	{
		if (AEdenRopeSolverActor* Solver = CachedSolver.Get())
		{
			return Solver->GetSingleParticleMass(RopeHandle, LocalIndex);
		}
	}

	return MassPerParticle;
}

void UEdenRopeComponentBase::BeginPlay()
{
	Super::BeginPlay();
	RegisterToSolver();
}

void UEdenRopeComponentBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromSolver();
	Super::EndPlay(EndPlayReason);
}

void UEdenRopeComponentBase::TickComponent(float DeltaTime, ELevelTick TickType,
                                            FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bSolverRegistered)
	{
		SyncFromSolver();
	}
	else
	{
#if WITH_EDITOR
		// When an editing SplineComponent exists (created by DetailCustomization),
		// sync its curves back to AuthoringCurves so the rope updates in real-time.
		SyncFromEditingSpline();
#endif
		InitializeDefaultParticlePositions();
		MarkRenderDynamicDataDirty();
		UpdateComponentToWorld();
	}

	if (RenderMode == ERopeRenderMode::SegmentMesh)
		UpdateSegmentMeshInstances();
}

#if WITH_EDITOR
void UEdenRopeComponentBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	DestroySegmentMeshISMC();
	if (RenderMode == ERopeRenderMode::SegmentMesh)
		CreateSegmentMeshISMC();
}
#endif

// ========== Segment Mesh ISMC Implementation ==========

void UEdenRopeComponentBase::CreateSegmentMeshISMC()
{
	if (!SegmentMesh || SegmentMeshISMC) return;

	SegmentMeshISMC = NewObject<UInstancedStaticMeshComponent>(GetOwner(),
		UInstancedStaticMeshComponent::StaticClass(), NAME_None, RF_Transient);
	SegmentMeshISMC->SetStaticMesh(SegmentMesh);
	SegmentMeshISMC->SetMobility(EComponentMobility::Movable);
	SegmentMeshISMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SegmentMeshISMC->SetupAttachment(this);
	SegmentMeshISMC->RegisterComponent();

	const int32 NumSegments = FMath::Max(0, CachedParticlePositions.Num() - 1);
	for (int32 i = 0; i < NumSegments; ++i)
		SegmentMeshISMC->AddInstance(FTransform::Identity);

	UpdateSegmentMeshInstances();
}

void UEdenRopeComponentBase::DestroySegmentMeshISMC()
{
	if (SegmentMeshISMC)
	{
		SegmentMeshISMC->DestroyComponent();
		SegmentMeshISMC = nullptr;
	}
}

void UEdenRopeComponentBase::UpdateSegmentMeshInstances()
{
	if (!SegmentMeshISMC || CachedParticlePositions.Num() < 2) return;

	const int32 NumSegments = CachedParticlePositions.Num() - 1;

	if (SegmentMeshISMC->GetInstanceCount() != NumSegments)
	{
		SegmentMeshISMC->ClearInstances();
		for (int32 i = 0; i < NumSegments; ++i)
			SegmentMeshISMC->AddInstance(FTransform::Identity);
	}

	const float RefLength = GetSegmentMeshRefLength();
	const int32 FwdIdx    = GetForwardAxisIndex();
	const FVector FwdVec  = GetForwardAxisVector();

	TArray<FTransform> Transforms;
	Transforms.Reserve(NumSegments);

	for (int32 i = 0; i < NumSegments; ++i)
	{
		const FVector& P0  = CachedParticlePositions[i];
		const FVector& P1  = CachedParticlePositions[i + 1];
		const FVector Dir  = P1 - P0;
		const float SegLen = Dir.Size();
		const FVector DirN = SegLen > KINDA_SMALL_NUMBER ? Dir / SegLen : FwdVec;
		const FVector Mid  = (P0 + P1) * 0.5f;

		FQuat Rot = FQuat::FindBetweenNormals(FwdVec, DirN);
		if (i % 2 == 1 && !FMath::IsNearlyZero(SegmentMeshTwistAngle))
		{
			const FQuat TwistRot(FwdVec, FMath::DegreesToRadians(SegmentMeshTwistAngle));
			Rot = Rot * TwistRot;
		}

		FVector Scale(1.0f);
		Scale[FwdIdx]            = (RefLength > KINDA_SMALL_NUMBER ? SegLen / RefLength : 1.0f)
		                           * SegmentMeshForwardScale;
		Scale[(FwdIdx + 1) % 3] = SegmentMeshCrossScale.X;
		Scale[(FwdIdx + 2) % 3] = SegmentMeshCrossScale.Y;

		Transforms.Add(FTransform(Rot, Mid, Scale));
	}

	SegmentMeshISMC->BatchUpdateInstancesTransforms(0, Transforms,
		/*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
}

float UEdenRopeComponentBase::GetSegmentMeshRefLength() const
{
	if (!SegmentMesh) return 1.0f;
	const FBoxSphereBounds MeshBounds = SegmentMesh->GetBounds();
	return MeshBounds.BoxExtent[GetForwardAxisIndex()] * 2.0f;
}

int32 UEdenRopeComponentBase::GetForwardAxisIndex() const
{
	switch (SegmentMeshForwardAxis)
	{
	case ESegmentMeshForwardAxis::X: return 0;
	case ESegmentMeshForwardAxis::Y: return 1;
	case ESegmentMeshForwardAxis::Z: return 2;
	default:                          return 0;
	}
}

FVector UEdenRopeComponentBase::GetForwardAxisVector() const
{
	switch (SegmentMeshForwardAxis)
	{
	case ESegmentMeshForwardAxis::X: return FVector::XAxisVector;
	case ESegmentMeshForwardAxis::Y: return FVector::YAxisVector;
	case ESegmentMeshForwardAxis::Z: return FVector::ZAxisVector;
	default:                          return FVector::XAxisVector;
	}
}

// ========== Editor: Real-time Spline Sync ==========

#if WITH_EDITOR
void UEdenRopeComponentBase::SyncFromEditingSpline()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Find a transient SplineComponent attached to this component.
	// The DetailCustomization creates exactly one when spline editing is active.
	TArray<USplineComponent*> SplineComps;
	Owner->GetComponents(SplineComps);

	for (USplineComponent* SplineComp : SplineComps)
	{
		if (SplineComp &&
			SplineComp->HasAnyFlags(RF_Transient) &&
			SplineComp->GetAttachParent() == this)
		{
			// SplineComponent 的世界变换 == RopeComponent 的世界变换（RelativeTransform 为 Identity），
			// 因此 SplineCurves 中的数据始终在 RopeComponent 局部空间中，与 Scale 无关。
			AuthoringCurves = SplineComp->SplineCurves;
			// 使用 FVector::OneVector 重建 ReparamTable，保证 Dist、SplineLen、ReparamTable 均为 Object Space
			AuthoringCurves.UpdateSpline(false, false, 10, false, 0.0f, FVector::OneVector);
			return;
		}
	}
}
#endif

// ========== Spline Authoring Helpers ==========

bool UEdenRopeComponentBase::SampleFromCurves(
	const FSplineCurves& Curves,
	int32 NumPts,
	float LengthCm,
	TArray<FVector>& OutPositions,
	TArray<FQuat>* OutOrientations) const
{
	if (NumPts < 2 || LengthCm <= 0.f || Curves.Position.Points.Num() < 2)
	{
		return false;
	}

	const FTransform& CompTransform = GetComponentTransform();
	const float SegLen = LengthCm / (NumPts - 1);
	const float SplineLen = Curves.GetSplineLength(); // local units (cm)

	// Compute end-point data for overshoot (when rope is longer than spline)
	const float EndParam = Curves.ReparamTable.Eval(SplineLen, 0.0f);
	const FVector EndLocalPos = Curves.Position.Eval(EndParam, FVector::ZeroVector);
	const FVector EndLocalTangent = Curves.Position.EvalDerivative(EndParam, FVector::ZeroVector).GetSafeNormal();

	OutPositions.Reset(NumPts);

	for (int32 i = 0; i < NumPts; ++i)
	{
		const float Dist = i * SegLen;
		if (Dist <= SplineLen)
		{
			const float Param = Curves.ReparamTable.Eval(Dist, 0.0f);
			const FVector LocalPos = Curves.Position.Eval(Param, FVector::ZeroVector);
			OutPositions.Add(CompTransform.TransformPosition(LocalPos));
		}
		else
		{
			// Overshoot: 超出 Spline 长度的部分，沿末端切线方向在局部空间延伸后变换到世界空间。
			// Overshoot 距离在局部空间中计算（与 SegLen 同单位），
			// 通过 TransformPosition 统一变换到世界空间（含 Scale），确保单位一致。
			const float Overshoot = Dist - SplineLen;
			OutPositions.Add(CompTransform.TransformPosition(EndLocalPos + EndLocalTangent * Overshoot));
		}
	}

	// Optional: compute per-segment orientations (for Rod)
	if (OutOrientations)
	{
		OutOrientations->Reset(NumPts - 1);
		for (int32 i = 0; i < NumPts - 1; ++i)
		{
			const FVector& P0 = OutPositions[i];
			const FVector& P1 = OutPositions[i + 1];
			const FVector Tangent = (P1 - P0).GetSafeNormal();
			// Z = Forward convention: find rotation from Z-axis to the tangent
			const FQuat Q = FQuat::FindBetweenNormals(FVector::ZAxisVector, Tangent);
			OutOrientations->Add(Q);
		}
	}

	return true;
}
