// Fill out your copyright notice in the Description page of Project Settings.

#include "RigidRope/EdenRigidRopeComponent.h"
#include "RigidRope/EdenRigidRopeAsset.h"
#include "Render/EdenRopeSceneProxy.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"
#include "SceneInterface.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EdenRigidRopeComponent)

static const FString RopeParticleSocketPrefix(TEXT("Particle_"));

// UE 5.6 renamed FBodyInstance::GetPhysicsActorHandle() to GetPhysicsActor().
// Wrap the accessor so this component also builds on UE 5.5 and earlier,
// where only GetPhysicsActorHandle() (returning a reference) is available.
static FORCEINLINE FPhysicsActorHandle EdenGetBodyPhysicsActor(const FBodyInstance* BI)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	return BI->GetPhysicsActor();
#else
	return BI->GetPhysicsActorHandle();
#endif
}

//////////////////////////////////////////////////////////////////////////
// FEdenRigidRopeEndPhysicsTickFunction

void FEdenRigidRopeEndPhysicsTickFunction::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	FActorComponentTickFunction::ExecuteTickHelper(Target, /*bTickInEditor=*/ false, DeltaTime, TickType, [this](float DilatedTime)
	{
		Target->EndPhysicsTickComponent(*this);
	});
}

FString FEdenRigidRopeEndPhysicsTickFunction::DiagnosticMessage()
{
	if (Target)
	{
		return Target->GetFullName() + TEXT("[EndPhysicsTick]");
	}
	return TEXT("<NULL>[EndPhysicsTick]");
}

FName FEdenRigidRopeEndPhysicsTickFunction::DiagnosticContext(bool bDetailed)
{
	return FName(TEXT("EdenRigidRopeEndPhysicsTick"));
}

//////////////////////////////////////////////////////////////////////////

// Sets default values for this component's properties
UEdenRigidRopeComponent::UEdenRigidRopeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetTickGroup(TG_PrePhysics);
	bTickInEditor = true;
	bAutoActivate = true;

	// Initialize EndPhysicsTickFunction (called after physics simulation)
	// These must be set in constructor for the tick function to work properly
	EndPhysicsTickFunction.TickGroup = TG_EndPhysics;
	EndPhysicsTickFunction.bCanEverTick = true;
	EndPhysicsTickFunction.bStartWithTickEnabled = true;

	SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);

	SetIsReplicatedByDefault(true);
}

void UEdenRigidRopeComponent::SetRopeAsset(UEdenRigidRopeAsset* NewAsset)
{
	if (RopeAsset == NewAsset)
	{
		return;
	}

	RopeAsset = NewAsset;

	// 刷新第一个 Particle 的 Socket 名称显示
	FirstParticleSocketName = RopeAsset ? RopeAsset->GetBoneName(0) : NAME_None;

	// 如果已经注册，需要销毁当前物理状态并重新初始化
	if (IsRegistered())
	{
		// 重新初始化粒子
		if (RopeAsset)
		{
			const int32 NewNumSegments = RopeAsset->NumSegments;
			Particles.Reset();
			Particles.AddUninitialized(NewNumSegments + 1);

			FVector StartPosition, EndPosition;
			GetEndPositions(StartPosition, EndPosition);

			for (int32 ParticleIdx = 0; ParticleIdx < NewNumSegments + 1; ParticleIdx++)
			{
				const float Alpha = static_cast<float>(ParticleIdx) / static_cast<float>(NewNumSegments);
				const FVector InitialPosition = FMath::Lerp(StartPosition, EndPosition, Alpha);

				FEdenRigidRopeParticle& Particle = Particles[ParticleIdx];
				Particle.bFree = (ParticleIdx > 0);
				Particle.Position = InitialPosition;
				Particle.OldPosition = InitialPosition;
			}
		}
		else
		{
			Particles.Reset();
		}

		// RecreatePhysicsState will call OnDestroyPhysicsState (which handles
		// external constraint cleanup and TermRopePhysics) then OnCreatePhysicsState
		// (which rebuilds bodies and re-inits external constraints).
		RopeBodySetup = nullptr;

		// \u5148\u9500\u6bc1\u65e7 ISMC\uff0c\u907f\u514d\u65e7 Asset \u7684 Mesh \u6b8b\u7559
		DestroySegmentMeshISMC();

		RecreatePhysicsState();

		// \u6839\u636e\u65b0 Asset \u7684 RenderMode \u51b3\u5b9a\u662f\u5426\u521b\u5efa\u65b0 ISMC
		CreateSegmentMeshISMC();

		// \u786e\u4fdd Tube \u2194 SegmentMesh \u5207\u6362\u65f6 FEdenRopeSceneProxy \u88ab\u6b63\u786e\u91cd\u5efa/\u9500\u6bc1
		MarkRenderStateDirty();
		MarkRenderDynamicDataDirty();
	}
}

void UEdenRigidRopeComponent::OnRep_RopeAsset()
{
	// After replicate RopeAsset, reset particle, physics, render state
	UEdenRigidRopeAsset* NewAsset = RopeAsset;
	RopeAsset = nullptr;
	SetRopeAsset(NewAsset);
}

void UEdenRigidRopeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// For Runtime Setup RopeAsset in Server&Client case, RopeAsset needs to be replicated
	DOREPLIFETIME(UEdenRigidRopeComponent, RopeAsset);
}

#if WITH_EDITOR
void UEdenRigidRopeComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeComponent, RopeAsset))
	{
		// 注意：不能调用 SetRopeAsset(RopeAsset)，因为 UE 属性系统已经更新了 RopeAsset 成员，
		// SetRopeAsset 中 RopeAsset == NewAsset 会 early return，导致 FirstParticleSocketName 不刷新。
		// 直接内联执行刷新逻辑。

		// 刷新第一个 Particle 的 Socket 名称显示
		FirstParticleSocketName = RopeAsset ? RopeAsset->GetBoneName(0) : NAME_None;

		if (IsRegistered())
		{
			// 重新初始化粒子
			if (RopeAsset)
			{
				const int32 NewNumSegments = RopeAsset->NumSegments;
				Particles.Reset();
				Particles.AddUninitialized(NewNumSegments + 1);

				FVector StartPosition, EndPosition;
				GetEndPositions(StartPosition, EndPosition);

				for (int32 ParticleIdx = 0; ParticleIdx < NewNumSegments + 1; ParticleIdx++)
				{
					const float Alpha = static_cast<float>(ParticleIdx) / static_cast<float>(NewNumSegments);
					const FVector InitialPosition = FMath::Lerp(StartPosition, EndPosition, Alpha);

					FEdenRigidRopeParticle& Particle = Particles[ParticleIdx];
					Particle.bFree = (ParticleIdx > 0);
					Particle.Position = InitialPosition;
					Particle.OldPosition = InitialPosition;
				}
			}
			else
			{
				Particles.Reset();
			}

			RopeBodySetup = nullptr;
			RecreatePhysicsState();
			MarkRenderDynamicDataDirty();
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeComponent, ForwardAxis))
	{
		if (IsRegistered())
		{
			UnregisterComponent();
			RegisterComponent();
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeComponent, bEnableSelfCollision))
	{
		// 重建物理状态，让 SetupSelfCollisionDisable 按新值重新登记/取消忽略关系
		if (IsRegistered() && HasValidPhysicsState())
		{
			RecreatePhysicsState();
		}
	}

	// 统一尾部处理：SegmentMesh 渲染相关属性任何变更都需要重建 ISMC，
	// 并通过 MarkRenderStateDirty 让 CreateSceneProxy 按最新 RenderMode 重新决定（Tube ↔ SegmentMesh 切换）
	if (IsRegistered())
	{
		DestroySegmentMeshISMC();
		CreateSegmentMeshISMC();
		MarkRenderStateDirty();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UEdenRigidRopeComponent::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	// 只关心当前引用的 RopeAsset 发生变化
	if (Object == nullptr || Object != RopeAsset)
	{
		return;
	}

	// RopeAsset 的属性被修改了，重新初始化组件以同步最新数据
	if (IsRegistered())
	{
		// 刷新第一个 Particle 的 Socket 名称显示
		FirstParticleSocketName = RopeAsset->GetBoneName(0);

		// 重新初始化粒子
		const int32 NumSegs = RopeAsset->NumSegments;
		const int32 NumParticles = NumSegs + 1;

		Particles.Reset();
		Particles.AddUninitialized(NumParticles);

		FVector StartPosition, EndPosition;
		GetEndPositions(StartPosition, EndPosition);

		for (int32 ParticleIdx = 0; ParticleIdx < NumParticles; ParticleIdx++)
		{
			const float Alpha = static_cast<float>(ParticleIdx) / static_cast<float>(NumSegs);
			const FVector InitialPosition = FMath::Lerp(StartPosition, EndPosition, Alpha);

			FEdenRigidRopeParticle& Particle = Particles[ParticleIdx];
			Particle.bFree = (ParticleIdx > 0);
			Particle.Position = InitialPosition;
			Particle.OldPosition = InitialPosition;
		}

		// RecreatePhysicsState handles destroy (with external constraint cleanup)
		// and create (with external constraint re-init) in the correct order.
		RopeBodySetup = nullptr;
		RecreatePhysicsState();

		// SegmentMesh 模式：Asset 内部属性变更（包括 RenderMode/SegmentMesh/Axis/Scale/Twist 等）
		// 需要重建 ISMC，以正确应用新参数
		DestroySegmentMeshISMC();
		CreateSegmentMeshISMC();

		MarkRenderStateDirty();
	}
}
#endif

FPrimitiveSceneProxy* UEdenRigidRopeComponent::CreateSceneProxy()
{
	// 如果没有 Asset 或没有粒子数据，不创建 SceneProxy
	if (!RopeAsset || Particles.Num() < 2)
	{
		return nullptr;
	}

	// SegmentMesh 模式：渲染由 ISMC 承担，不创建 Tube SceneProxy
	if (ShouldUseSegmentMeshRender())
	{
		return nullptr;
	}

	return new FEdenRopeSceneProxy(this);
}

UBodySetup* UEdenRigidRopeComponent::GetBodySetup()
{
	return RopeBodySetup;
}

bool UEdenRigidRopeComponent::CanEditSimulatePhysics()
{
	// 绳索组件始终允许编辑 Simulate Physics，
	// 不依赖 RopeBodySetup 是否已创建（它在 OnRegister 中延迟创建，依赖 RopeAsset）
	return true;
}

FBodyInstance* UEdenRigidRopeComponent::GetBodyInstance(FName BoneName, bool bGetWelded, int32 Index) const
{
	if (BoneName == NAME_None && Bodies.Num() > 0)
	{
		return Bodies[0];
	}

	// Primary path: resolve via Asset's BodySetupIndexMap (matches UBodySetup::BoneName directly)
	if (RopeAsset)
	{
		const int32 BodyIndex = RopeAsset->FindBodyIndex(BoneName);
		if (BodyIndex != INDEX_NONE)
		{
			return GetBodyByIndex(BodyIndex);
		}
	}

	// Secondary path: resolve via socket name parsing (handles legacy "Particle_" prefix too)
	if (FBodyInstance* FoundBody = GetBodyBySocketName(BoneName))
	{
		return FoundBody;
	}
	
	return Super::GetBodyInstance(BoneName, bGetWelded, Index);
}

int32 UEdenRigidRopeComponent::GetNumMaterials() const
{
	return 1;
}

//////////////////////////////////////////////////////////////////////////
// 多 Body：速度/位置/旋转
// (SetAllPhysicsPosition 和 SetAllPhysicsRotation 在文件后部)

void UEdenRigidRopeComponent::SetAllPhysicsLinearVelocity(FVector NewVel, bool bAddToCurrent)
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->SetLinearVelocity(NewVel, bAddToCurrent);
		}
	}
}

void UEdenRigidRopeComponent::SetAllPhysicsAngularVelocityInRadians(const FVector& NewAngVel, bool bAddToCurrent)
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->SetAngularVelocityInRadians(NewAngVel, bAddToCurrent);
		}
	}
}

void UEdenRigidRopeComponent::SetAllLinearDamping(float InDamping)
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->LinearDamping = InDamping;
			BI->UpdateDampingProperties();
		}
	}
}

void UEdenRigidRopeComponent::SetAllAngularDamping(float InDamping)
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->AngularDamping = InDamping;
			BI->UpdateDampingProperties();
		}
	}
}

void UEdenRigidRopeComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITOR
	// 监听 RopeAsset 的属性变更，以便在 Asset 被编辑时自动同步更新组件
	if (!AssetPropertyChangedHandle.IsValid())
	{
		AssetPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(
			this, &UEdenRigidRopeComponent::OnObjectPropertyChanged);
	}
#endif

	// 如果没有 Asset，跳过粒子和物理初始化
	if (!RopeAsset)
	{
		Particles.Reset();
		return;
	}

	const int32 NumSegs = RopeAsset->NumSegments;
	const int32 NumParticles = NumSegs + 1;

	Particles.Reset();
	Particles.AddUninitialized(NumParticles);

	// Initialize particles in a straight line, considering component rotation
	FVector RopeStart, RopeEnd;
	GetEndPositions(RopeStart, RopeEnd);

	const FVector Delta = RopeEnd - RopeStart;

	for(int32 ParticleIdx=0; ParticleIdx<NumParticles; ParticleIdx++)
	{
		FEdenRigidRopeParticle& Particle = Particles[ParticleIdx];

		const float Alpha = (float)ParticleIdx/(float)NumSegs;
		const FVector InitialPosition = RopeStart + (Alpha * Delta);

		Particle.Position = InitialPosition;
		Particle.OldPosition = InitialPosition;
		Particle.bFree = true;
	}

	// Always create BodySetup for collision visualization (even when physics is disabled)
	CreateRopeBodySetup();

	// 刷新第一个 Particle 的 Socket 名称显示
	FirstParticleSocketName = RopeAsset ? RopeAsset->GetBoneName(0) : NAME_None;

	// 初始化 ParticleSpaceTransforms 缓存
	UpdateParticleSpaceTransforms();

	// 从 Particle 数据推导 Body[0] 的预期世界变换，初始化 TransformToRoot
	// 此时物理体尚未创建（OnCreatePhysicsState 在 OnRegister 之后），
	// 所以需要从 Particle 位置推导 Body[0] 的变换
	if (Particles.Num() >= 2)
	{
		const FVector Forward = (Particles[1].Position - Particles[0].Position).GetSafeNormal();
		const FQuat Body0Rot = FQuat::FindBetweenNormals(FVector::ForwardVector, Forward);
		const FTransform Body0PredictedTransform(Body0Rot, Particles[0].Position);
		TransformToRoot = GetComponentTransform().GetRelativeTransform(Body0PredictedTransform);
	}
	else
	{
		TransformToRoot = FTransform::Identity;
	}

	// SegmentMesh 渲染路径：在粒子初始化完成后创建 ISMC 并同步实例
	CreateSegmentMeshISMC();
}

void UEdenRigidRopeComponent::OnUnregister()
{
#if WITH_EDITOR
	if (AssetPropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(AssetPropertyChangedHandle);
		AssetPropertyChangedHandle.Reset();
	}
#endif

	// 先销毁 ISMC，避免父类清理后残留
	DestroySegmentMeshISMC();

	Super::OnUnregister();
}

void UEdenRigidRopeComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	TermRopePhysics();
	DestroySegmentMeshISMC();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

int32 UEdenRigidRopeComponent::ParseBoneIndex(FName InName) const
{
	const FString NameStr = InName.ToString();
	const int32 NumParticlesCount = Particles.Num();

	// Try Asset's BoneNamePrefix first (e.g. "StringBone_3")
	if (RopeAsset && !RopeAsset->BoneNamePrefix.IsEmpty())
	{
		const FString Prefix = RopeAsset->BoneNamePrefix + TEXT("_");
		if (NameStr.StartsWith(Prefix))
		{
			const FString IndexStr = NameStr.RightChop(Prefix.Len());
			if (IndexStr.Len() > 0 && FCString::IsNumeric(*IndexStr))
			{
				const int32 Index = FCString::Atoi(*IndexStr);
				if (Index >= 0 && Index < NumParticlesCount)
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}
	}

	// Fallback: legacy "Particle_" prefix
	if (NameStr.StartsWith(RopeParticleSocketPrefix))
	{
		const FString IndexStr = NameStr.RightChop(RopeParticleSocketPrefix.Len());
		if (IndexStr.Len() > 0 && FCString::IsNumeric(*IndexStr))
		{
			const int32 Index = FCString::Atoi(*IndexStr);
			if (Index >= 0 && Index < NumParticlesCount)
			{
				return Index;
			}
		}
	}

	return INDEX_NONE;
}

FVector UEdenRigidRopeComponent::GetLocalForwardVector() const
{
	switch (ForwardAxis)
	{
		case EEdenRopeForwardAxis::PositiveX: return FVector( 1, 0, 0);
		case EEdenRopeForwardAxis::NegativeX: return FVector(-1, 0, 0);
		case EEdenRopeForwardAxis::PositiveY: return FVector( 0, 1, 0);
		case EEdenRopeForwardAxis::NegativeY: return FVector( 0,-1, 0);
		case EEdenRopeForwardAxis::PositiveZ: return FVector( 0, 0, 1);
		case EEdenRopeForwardAxis::NegativeZ: return FVector( 0, 0,-1);
		default: return FVector(1, 0, 0);
	}
}

void UEdenRigidRopeComponent::GetEndPositions(FVector& OutStartPosition, FVector& OutEndPosition) const
{
	OutStartPosition = GetComponentLocation();

	const float CurrentRopeLength = RopeAsset ? RopeAsset->RopeLength : 0.f;
	OutEndPosition = GetComponentTransform().TransformPosition(GetLocalForwardVector() * CurrentRopeLength);
}

void UEdenRigidRopeComponent::ApplyWorldOffset(const FVector & InOffset, bool bWorldShift)
{
	Super::ApplyWorldOffset(InOffset, bWorldShift);

	for (FEdenRigidRopeParticle& Particle : Particles)
	{
		Particle.Position += InOffset;
		Particle.OldPosition += InOffset;
	}
}

void UEdenRigidRopeComponent::SendRenderDynamicData_Concurrent()
{
	// SegmentMesh 模式：不使用 FEdenRopeSceneProxy，直接返回
	if (ShouldUseSegmentMeshRender())
	{
		return;
	}

	if(SceneProxy)
	{
		// Allocate rope dynamic data
		FEdenRopeRenderDynamicData* DynamicData = new FEdenRopeRenderDynamicData;

		// Transform current positions from particles into component-space array
		const FTransform& ComponentTransform = GetComponentTransform();
		const int32 NumSegs = RopeAsset ? RopeAsset->NumSegments : 0;
		int32 NumPoints = NumSegs + 1;
		DynamicData->RopePoints.AddUninitialized(NumPoints);
		for(int32 PointIdx=0; PointIdx<NumPoints; PointIdx++)
		{
			DynamicData->RopePoints[PointIdx] = ComponentTransform.InverseTransformPosition(Particles[PointIdx].Position);
		}

		// 碰撞体变换统一从 Particles 数组计算（类似 SkeletalMeshComponent 使用 bone transforms 而非 Body->GetUnrealWorldTransform()）
		// 这样无论物理体是否因 kinematic target 延迟而未更新，碰撞体渲染始终与绳索视觉位置一致
		if (Particles.Num() > 1 && RopeAsset)
		{
			DynamicData->BodyTransforms.Reserve(NumSegs);
			for (int32 i = 0; i < NumSegs; i++)
			{
				const FVector& StartPos = Particles[i].Position;
				const FVector& EndPos = Particles[i + 1].Position;
				
				const FVector Direction = (EndPos - StartPos).GetSafeNormal();
				const FQuat Rotation = FQuat::FindBetweenNormals(FVector::ForwardVector, Direction);
				
				// Body 原点位于 Particle 端点（与 BodyInstance 空间一致）
				DynamicData->BodyTransforms.Add(FTransform(Rotation, StartPos));
			}
		}

		// Enqueue command to send to render thread
		FEdenRopeSceneProxy* RopeSceneProxy = (FEdenRopeSceneProxy*)SceneProxy;
		ENQUEUE_RENDER_COMMAND(FSendRopeDynamicData)(UE::RenderCommandPipe::EdenRope,
			[RopeSceneProxy, DynamicData](FRHICommandListBase& RHICmdList)
		{
			RopeSceneProxy->SetDynamicData_RenderThread(RHICmdList, DynamicData);
		});
	}
}

FBoxSphereBounds UEdenRigidRopeComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Calculate bounding box of rope points
	FBox RopeBox(ForceInit);

	const FTransform& ComponentTransform = GetComponentTransform();

	for(int32 ParticleIdx=0; ParticleIdx<Particles.Num(); ParticleIdx++)
	{
		const FEdenRigidRopeParticle& Particle = Particles[ParticleIdx];
		RopeBox += ComponentTransform.InverseTransformPosition(Particle.Position);
	}

	// Expand by rope radius (half rope width)
	const float CurrentCableWidth = RopeAsset ? RopeAsset->CableWidth : 5.f;
	return FBoxSphereBounds(RopeBox.ExpandBy(0.5f * CurrentCableWidth)).TransformBy(LocalToWorld);
}

void UEdenRigidRopeComponent::QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const 
{
	// Add bone/socket for each particle using Asset prefix (or legacy "Particle_" fallback)
	for (int32 i = 0; i < Particles.Num(); ++i)
	{
		const FName BoneName = RopeAsset ? RopeAsset->GetBoneName(i)
		                                 : FName(*(RopeParticleSocketPrefix + FString::FromInt(i)));
		OutSockets.Add(FComponentSocketDescription(BoneName, EComponentSocketType::Socket));
	}

	// Asset-defined custom sockets
	if (RopeAsset)
	{
		for (const FEdenRopeSocket& RopeSocket : RopeAsset->Sockets)
		{
			OutSockets.Add(FComponentSocketDescription(RopeSocket.SocketName, EComponentSocketType::Socket));
		}
	}
}

FTransform UEdenRigidRopeComponent::GetSocketTransform(FName InSocketName, ERelativeTransformSpace TransformSpace) const
{
	// Try parsing bone index from name (supports both Asset prefix and legacy "Particle_")
	const int32 ParticleIndex = ParseBoneIndex(InSocketName);
	if (ParticleIndex != INDEX_NONE && ParticleSpaceTransforms.IsValidIndex(ParticleIndex))
	{
		// 优先从 ParticleSpaceTransforms 缓存读取（与 BodyInstance 空间一致）
		const FTransform& ParticleCS = ParticleSpaceTransforms[ParticleIndex];

		switch (TransformSpace)
		{
			case RTS_World:
			{
				return ParticleCS * GetComponentTransform();
			}
			case RTS_Actor:
			{
				if (const AActor* Actor = GetOwner())
				{
					return (ParticleCS * GetComponentTransform()).GetRelativeTransform(Actor->GetTransform());
				}
				break;
			}
			case RTS_Component:
			{
				return ParticleCS;
			}
		}

		return ParticleCS * GetComponentTransform();
	}

	// Check Asset-defined custom sockets
	if (RopeAsset)
	{
		if (const FEdenRopeSocket* RopeSocket = RopeAsset->FindSocket(InSocketName))
		{
			const int32 BodyIdx = RopeSocket->ParentBodyIndex;
			FTransform SocketWorldTM = FTransform::Identity;

			if (Bodies.IsValidIndex(BodyIdx) && Bodies[BodyIdx] && Bodies[BodyIdx]->IsValidBodyInstance())
			{
				FTransform BodyWorldTM = Bodies[BodyIdx]->GetUnrealWorldTransform();
				SocketWorldTM = RopeSocket->RelativeTransform * BodyWorldTM;
			}
			else if (ParticleSpaceTransforms.IsValidIndex(BodyIdx))
			{
				// 无物理体时，从 ParticleSpaceTransforms 缓存推导 Body 世界变换
				FTransform BodyWorldTM = ParticleSpaceTransforms[BodyIdx] * GetComponentTransform();
				SocketWorldTM = RopeSocket->RelativeTransform * BodyWorldTM;
			}
			else
			{
				return Super::GetSocketTransform(InSocketName, TransformSpace);
			}

			switch (TransformSpace)
			{
				case RTS_World:
				{
					return SocketWorldTM;
				}
				case RTS_Actor:
				{
					if (const AActor* Actor = GetOwner())
					{
						return SocketWorldTM.GetRelativeTransform(Actor->GetTransform());
					}
					break;
				}
				case RTS_Component:
				{
					return SocketWorldTM.GetRelativeTransform(GetComponentTransform());
				}
			}

			return SocketWorldTM;
		}
	}

	return Super::GetSocketTransform(InSocketName, TransformSpace);
}

bool UEdenRigidRopeComponent::HasAnySockets() const
{
	return (Particles.Num() >= 2);
}

bool UEdenRigidRopeComponent::DoesSocketExist(FName InSocketName) const
{
	if (ParseBoneIndex(InSocketName) != INDEX_NONE)
	{
		return true;
	}

	if (RopeAsset && RopeAsset->FindSocket(InSocketName))
	{
		return true;
	}

	return false;
}

void UEdenRigidRopeComponent::GetRopeParticleLocations(TArray<FVector>& OutLocations) const
{
	OutLocations.Empty();
	for (const FEdenRigidRopeParticle& Particle : Particles)
	{
		OutLocations.Add(Particle.Position);
	}
}

void UEdenRigidRopeComponent::GetRopeBodyTransforms(TArray<FTransform>& OutTransforms) const
{
	OutTransforms.Empty();
	
	if (Bodies.Num() > 0)
	{
		// Physics is running - get transforms from physics bodies
		OutTransforms.Reserve(Bodies.Num());
		for (FBodyInstance* Body : Bodies)
		{
			if (Body && Body->IsValidBodyInstance())
			{
				OutTransforms.Add(Body->GetUnrealWorldTransform());
			}
		}
	}
	else if (Particles.Num() > 1)
	{
		// Physics not running - calculate transforms from particles
		OutTransforms.Reserve(RopeAsset ? RopeAsset->NumSegments : 0);
		const int32 NumSegs = RopeAsset ? RopeAsset->NumSegments : 0;
		for (int32 i = 0; i < NumSegs && i + 1 < Particles.Num(); i++)
		{
			const FVector& StartPos = Particles[i].Position;
			const FVector& EndPos = Particles[i + 1].Position;
			
			const FVector Direction = (EndPos - StartPos).GetSafeNormal();
			const FQuat Rotation = FQuat::FindBetweenNormals(FVector::ForwardVector, Direction);
			
			// Body 原点位于 Particle 端点（与 BodyInstance 空间一致）
		OutTransforms.Add(FTransform(Rotation, StartPos));
		}
	}
}

FVector UEdenRigidRopeComponent::GetRopeStartWorldLocation() const
{
	if (Particles.Num() > 0)
	{
		return Particles[0].Position;
	}
	return GetComponentLocation();
}

FVector UEdenRigidRopeComponent::GetRopeEndWorldLocation() const
{
	if (Particles.Num() > 0)
	{
		return Particles.Last().Position;
	}
	return GetComponentLocation();
}

FBodyInstance* UEdenRigidRopeComponent::GetBodyBySocketName(FName SocketName) const
{
	const int32 ParticleIndex = ParseBoneIndex(SocketName);
	if (ParticleIndex != INDEX_NONE)
	{
		return GetBodyByIndex(ParticleIndex);
	}

	return nullptr;
}

FBodyInstance* UEdenRigidRopeComponent::GetBodyByIndex(int32 Index) const
{
	if (Bodies.IsValidIndex(Index))
	{
		return Bodies[Index];
	}
	return nullptr;
}

Chaos::FPhysicsObject* UEdenRigidRopeComponent::GetPhysicsObjectById(Chaos::FPhysicsObjectId Id) const
{
	if (!Bodies.IsValidIndex(Id) || !Bodies[Id] || !Bodies[Id]->ActorHandle)
	{
		return nullptr;
	}
	return Bodies[Id]->ActorHandle->GetPhysicsObject();
}

Chaos::FPhysicsObject* UEdenRigidRopeComponent::GetPhysicsObjectByName(const FName& Name) const
{
	// Use existing GetBodyBySocketName to find the body by socket name (e.g., "Particle_5")
	FBodyInstance* Body = GetBodyBySocketName(Name);
	if (!Body || !Body->ActorHandle)
	{
		return nullptr;
	}
	return Body->ActorHandle->GetPhysicsObject();
}

TArray<Chaos::FPhysicsObject*> UEdenRigidRopeComponent::GetAllPhysicsObjects() const
{
	TArray<Chaos::FPhysicsObject*> Objects;
	Objects.Reserve(Bodies.Num());
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		Objects.Add(GetPhysicsObjectById(Index));
	}
	return Objects;
}

void UEdenRigidRopeComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);

	FRegisterComponentContext::SendRenderDynamicData(Context, this);
}

//////////////////////////////////////////////////////////////////////////
// 多 Body：质量/材质/CCD

float UEdenRigidRopeComponent::GetMass() const
{
	float Mass = 0.0f;
	for (int32 i=0; i < Bodies.Num(); ++i)
	{
		FBodyInstance* BI = Bodies[i];

		if (BI && BI->IsValidBodyInstance())
		{
			Mass += BI->GetBodyMass();
		}
	}
	return Mass;
}

void UEdenRigidRopeComponent::SetAllUseCCD(bool InUseCCD)
{
	// Apply CCD setting to each child body
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->SetUseCCD(InUseCCD);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// 多 Body：物理模拟控制与状态查询

void UEdenRigidRopeComponent::SetSimulatePhysics(bool bSimulate)
{
	// 更新组件级配置值
	BodyInstance.bSimulatePhysics = bSimulate;

	// 遍历所有 Bodies 设置模拟状态
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->SetInstanceSimulatePhysics(bSimulate, false, true);
		}
	}

	UpdateEndPhysicsTickRegisteredState();
}

bool UEdenRigidRopeComponent::IsSimulatingPhysics(FName BoneName) const
{
	if (FBodyInstance* BI = GetBodyInstance(BoneName))
	{
		return BI->IsInstanceSimulatingPhysics();
	}
	return false;
}

bool UEdenRigidRopeComponent::IsAnySimulatingPhysics() const
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsInstanceSimulatingPhysics())
		{
			return true;
		}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
// 多 Body：唤醒/休眠

void UEdenRigidRopeComponent::WakeAllRigidBodies()
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->WakeInstance();
		}
	}
}

void UEdenRigidRopeComponent::PutAllRigidBodiesToSleep()
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->PutInstanceToSleep();
		}
	}
}

bool UEdenRigidRopeComponent::IsAnyRigidBodyAwake()
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance() && BI->IsInstanceAwake())
		{
			return true;
		}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
// 多 Body：碰撞设置

void UEdenRigidRopeComponent::SetNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision)
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->SetInstanceNotifyRBCollision(bNewNotifyRigidBodyCollision);
		}
	}

	if (Bodies.Num() > 0)
	{
		OnComponentCollisionSettingsChanged();
	}
}

void UEdenRigidRopeComponent::OnComponentCollisionSettingsChanged(bool bUpdateOverlaps)
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->UpdatePhysicsFilterData();
		}
	}

	Super::OnComponentCollisionSettingsChanged(bUpdateOverlaps);
}

void UEdenRigidRopeComponent::UpdatePhysicsToRBChannels()
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->UpdatePhysicsFilterData();
		}
	}
}

#if WITH_EDITOR
void UEdenRigidRopeComponent::UpdateCollisionProfile()
{
	Super::UpdateCollisionProfile();

	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->UpdatePhysicsFilterData();
		}
	}
}
#endif

//////////////////////////////////////////////////////////////////////////
// 多 Body：力/冲量

void UEdenRigidRopeComponent::AddRadialImpulse(FVector Origin, float Radius, float Strength, ERadialImpulseFalloff Falloff, bool bVelChange)
{
	if (bIgnoreRadialImpulse)
	{
		return;
	}

	const float TotalMass = GetMass();
	const float StrengthPerMass = Strength / FMath::Max(TotalMass, UE_KINDA_SMALL_NUMBER);

	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			const float StrengthPerBody = bVelChange ? Strength : (StrengthPerMass * BI->GetBodyMass());
			BI->AddRadialImpulseToBody(Origin, Radius, StrengthPerBody, Falloff, bVelChange);
		}
	}
}

void UEdenRigidRopeComponent::AddRadialForce(FVector Origin, float Radius, float Strength, ERadialImpulseFalloff Falloff, bool bAccelChange)
{
	if (bIgnoreRadialForce)
	{
		return;
	}

	const float TotalMass = GetMass();
	const float StrengthPerMass = Strength / FMath::Max(TotalMass, UE_KINDA_SMALL_NUMBER);

	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			const float StrengthPerBody = bAccelChange ? Strength : (StrengthPerMass * BI->GetBodyMass());
			BI->AddRadialForceToBody(Origin, Radius, StrengthPerBody, Falloff, bAccelChange);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// 多 Body：速度/位置/旋转
// (SetAllPhysicsLinearVelocity 和 SetAllPhysicsAngularVelocityInRadians 在文件前部)

void UEdenRigidRopeComponent::SetAllPhysicsPosition(FVector NewPos)
{
	if (Bodies.Num() == 0)
	{
		return;
	}

	// 使用根 body (Bodies[0]) 计算增量
	FBodyInstance* RootBI = Bodies[0];
	if (!RootBI || !RootBI->IsValidBodyInstance())
	{
		return;
	}

	FTransform RootBodyTM = RootBI->GetUnrealWorldTransform();
	FVector DeltaLoc = NewPos - RootBodyTM.GetLocation();
	RootBodyTM.SetTranslation(NewPos);
	RootBI->SetBodyTransform(RootBodyTM, ETeleportType::TeleportPhysics);

	// 将增量应用到所有其他 body
	for (int32 i = 1; i < Bodies.Num(); i++)
	{
		FBodyInstance* BI = Bodies[i];
		if (BI && BI->IsValidBodyInstance())
		{
			FTransform BodyTM = BI->GetUnrealWorldTransform();
			BodyTM.SetTranslation(BodyTM.GetTranslation() + DeltaLoc);
			BI->SetBodyTransform(BodyTM, ETeleportType::TeleportPhysics);
		}
	}

	// 同步组件变换到物理位置
	SyncComponentToRBPhysics();
}

void UEdenRigidRopeComponent::SetAllPhysicsRotation(FRotator NewRot)
{
	SetAllPhysicsRotation(NewRot.Quaternion());
}

void UEdenRigidRopeComponent::SetAllPhysicsRotation(const FQuat& NewRot)
{
	if (Bodies.Num() == 0)
	{
		return;
	}

	// 使用根 body (Bodies[0]) 计算增量旋转
	FBodyInstance* RootBI = Bodies[0];
	if (!RootBI || !RootBI->IsValidBodyInstance())
	{
		return;
	}

	FTransform RootBodyTM = RootBI->GetUnrealWorldTransform();
	FQuat DeltaQuat = RootBodyTM.GetRotation().Inverse() * NewRot;
	FVector RootLoc = RootBodyTM.GetLocation();
	RootBodyTM.SetRotation(NewRot);
	RootBI->SetBodyTransform(RootBodyTM, ETeleportType::TeleportPhysics);

	// 将增量旋转应用到所有其他 body
	for (int32 i = 1; i < Bodies.Num(); i++)
	{
		FBodyInstance* BI = Bodies[i];
		if (BI && BI->IsValidBodyInstance())
		{
			FTransform BodyTM = BI->GetUnrealWorldTransform();
			// 围绕根 body 位置旋转
			FVector RelPos = BodyTM.GetTranslation() - RootLoc;
			FVector NewRelPos = DeltaQuat.RotateVector(RelPos);
			BodyTM.SetTranslation(RootLoc + NewRelPos);
			BodyTM.SetRotation(BodyTM.GetRotation() * DeltaQuat);
			BI->SetBodyTransform(BodyTM, ETeleportType::TeleportPhysics);
		}
	}

	// 同步组件变换到物理位置
	SyncComponentToRBPhysics();
}

//////////////////////////////////////////////////////////////////////////
// 多 Body：质量/材质/CCD
// (GetMass 和 SetAllUseCCD 在文件前部)

void UEdenRigidRopeComponent::SetAllMassScale(float InMassScale)
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->SetMassScale(InMassScale);
		}
	}
}

void UEdenRigidRopeComponent::SetPhysMaterialOverride(UPhysicalMaterial* NewPhysMaterial)
{
	// 先调用基类更新 BodyInstance 的物理材质
	Super::SetPhysMaterialOverride(NewPhysMaterial);

	// 再遍历所有子 body 更新物理材质
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->UpdatePhysicalMaterials();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// 多 Body：碰撞查询

bool UEdenRigidRopeComponent::LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params)
{
	bool bHaveHit = false;
	float MinTime = UE_MAX_FLT;
	FHitResult Hit;

	for (int32 BodyIdx = 0; BodyIdx < Bodies.Num(); ++BodyIdx)
	{
		if (Bodies[BodyIdx] && Bodies[BodyIdx]->IsValidBodyInstance() &&
			Bodies[BodyIdx]->LineTrace(Hit, Start, End, Params.bTraceComplex, Params.bReturnPhysicalMaterial))
		{
			bHaveHit = true;
			if (MinTime > Hit.Time)
			{
				MinTime = Hit.Time;
				OutHit = Hit;
			}
		}
	}

	return bHaveHit;
}

bool UEdenRigidRopeComponent::SweepComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FQuat& ShapeWorldRotation, const FCollisionShape& CollisionShape, bool bTraceComplex)
{
	bool bHaveHit = false;
	FHitResult Hit;

	for (int32 BodyIdx = 0; BodyIdx < Bodies.Num(); ++BodyIdx)
	{
		if (Bodies[BodyIdx] && Bodies[BodyIdx]->IsValidBodyInstance() &&
			Bodies[BodyIdx]->Sweep(Hit, Start, End, ShapeWorldRotation, CollisionShape, bTraceComplex))
		{
			if (!bHaveHit || Hit.Time < OutHit.Time)
			{
				OutHit = Hit;
			}
			bHaveHit = true;
		}
	}

	return bHaveHit;
}

bool UEdenRigidRopeComponent::OverlapComponent(const FVector& Pos, const FQuat& Rot, const FCollisionShape& CollisionShape) const
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance() && BI->OverlapTest(Pos, Rot, CollisionShape))
		{
			return true;
		}
	}
	return false;
}

bool UEdenRigidRopeComponent::GetSquaredDistanceToCollision(const FVector& Point, float& OutSquaredDistance, FVector& OutClosestPointOnCollision) const
{
	OutClosestPointOnCollision = Point;
	bool bHasResult = false;

	for (int32 BodyIdx = 0; BodyIdx < Bodies.Num(); ++BodyIdx)
	{
		FBodyInstance* BI = Bodies[BodyIdx];
		if (BI && BI->IsValidBodyInstance() && (BI->GetCollisionEnabled() != ECollisionEnabled::NoCollision))
		{
			FVector ClosestPoint;
			float DistanceSqr = -1.f;

			if (!BI->GetSquaredDistanceToBody(Point, DistanceSqr, ClosestPoint))
			{
				continue;
			}

			if (!bHasResult || (DistanceSqr < OutSquaredDistance))
			{
				bHasResult = true;
				OutSquaredDistance = DistanceSqr;
				OutClosestPointOnCollision = ClosestPoint;

				// 如果在碰撞体内部，不可能找到更好的结果
				if (DistanceSqr <= UE_KINDA_SMALL_NUMBER)
				{
					break;
				}
			}
		}
	}

	return bHasResult;
}

FTransform UEdenRigidRopeComponent::GetComponentTransformFromBodyInstance(FBodyInstance* UseBI)
{
	// 参考 SKM 的实现：使用初始化时一次性计算的 TransformToRoot 还原 Component 变换。
	// TransformToRoot = ComponentTransform_Init * RootBodyTransform_Init.Inverse()
	// 因此：NewComponentTransform = TransformToRoot * RootBody_CurrentWorldTransform
	// 
	// 与之前使用 ParticleSpaceTransforms[0] 的区别：
	// TransformToRoot 在物理模拟期间保持不变（仅在初始化和 Teleport 时更新），
	// 避免了"每帧用旧 ComponentTransform 重算 → 总是返回旧值"的循环依赖。
	if (Bodies.Num() > 0 && UseBI == Bodies[0])
	{
		const FTransform& BodyWorldTransform = UseBI->GetUnrealWorldTransform();
		return TransformToRoot * BodyWorldTransform;
	}

	return Super::GetComponentTransformFromBodyInstance(UseBI);
}

bool UEdenRigidRopeComponent::ShouldCreatePhysicsState() const
{
	// Always create physics state when we have a valid Asset, even when not simulating.
	// Bodies are created as kinematic by default, allowing external PhysicsConstraints
	// to bind reliably. Simulation is enabled separately in OnCreatePhysicsState().
	// This mirrors USkeletalMeshComponent's pattern.
	return RopeAsset != nullptr && Super::ShouldCreatePhysicsState();
}

void UEdenRigidRopeComponent::CreateRopeBodySetup()
{
	if (RopeBodySetup != nullptr)
	{
		return; // Already created
	}

	if (!RopeAsset)
	{
		return;
	}

	// 使用 Asset 中的第一个 BodySetup 作为共享的碰撞可视化 BodySetup
	// 如果 Asset 中有 BodySetups，直接使用；否则创建一个临时的
	if (RopeAsset->BodySetups.Num() > 0 && RopeAsset->BodySetups[0])
	{
		RopeBodySetup = RopeAsset->BodySetups[0];
	}
	else
	{
		// Fallback: 创建一个临时的 BodySetup
		RopeBodySetup = NewObject<UBodySetup>(this, NAME_None, RF_Transient);
		RopeBodySetup->BodySetupGuid = FGuid::NewGuid();
		RopeBodySetup->bGenerateMirroredCollision = false;
		RopeBodySetup->bDoubleSidedGeometry = false;
		RopeBodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;

		const float SegmentLength = RopeAsset->GetSegmentLength();
		const float CapsuleRadius = RopeAsset->CableWidth * DebugPhysicsThickness / 2.0f;

		FKSphylElem CapsuleElem;
		CapsuleElem.Center = FVector::ZeroVector;
		CapsuleElem.Rotation = FRotator(90.f, 0.f, 0.f);
		CapsuleElem.Rotation += DebugPhysicsDeltaRotation;
		CapsuleElem.Radius = CapsuleRadius;
		CapsuleElem.Length = FMath::Max(0.0f, SegmentLength - 2.0f * CapsuleRadius);
		
		RopeBodySetup->AggGeom.SphylElems.Add(CapsuleElem);
		RopeBodySetup->CreatePhysicsMeshes();
	}
}

void UEdenRigidRopeComponent::InitRopeBodies(FPhysScene* PhysScene)
{
	if (!RopeAsset)
	{
		return;
	}

	const int32 NumSegs = RopeAsset->NumSegments;
	const int32 NumBodies = NumSegs;
	Bodies.Reset();
	Bodies.AddZeroed(NumBodies);

	const float SegmentLength = RopeAsset->GetSegmentLength();
	const float HalfLength = SegmentLength / 2.0f;

	for (int32 i = 0; i < NumBodies; i++)
	{
		// Body 原点位于 Particle[i] 端点（参考 SkeletalMesh 的 Bone==Body 空间约定）
		// Capsule 碰撞体通过 BodySetup 中的 CapsuleElem.Center 偏移到 Segment 中心
		const FVector& StartPos = Particles[i].Position;
		const FVector& EndPos = Particles[i + 1].Position;
		
		const FVector Direction = (EndPos - StartPos).GetSafeNormal();
		const FQuat Rotation = FQuat::FindBetweenNormals(FVector::ForwardVector, Direction);
		
		FTransform BodyTransform(Rotation, StartPos);

		// Create BodyInstance
		Bodies[i] = new FBodyInstance;
		FBodyInstance* BodyInst = Bodies[i];
		check(BodyInst);

		BodyInst->bSimulatePhysics = false;
		BodyInst->InstanceBodyIndex = i;
		BodyInst->bStartAwake = BodyInstance.bStartAwake;
		BodyInst->SetMassOverride(RopeAsset->SegmentMass, true);
		BodyInst->SetCollisionProfileName(GetCollisionProfileName());

		// 使用 Asset 中对应索引的 BodySetup，如果有的话
		UBodySetup* BodySetupToUse = RopeBodySetup;
		if (RopeAsset->BodySetups.IsValidIndex(i) && RopeAsset->BodySetups[i])
		{
			BodySetupToUse = RopeAsset->BodySetups[i];
		}

		FInitBodySpawnParams SpawnParams(this);
		SpawnParams.bStaticPhysics = false;
		SpawnParams.Aggregate = Aggregate;

		BodyInst->InitBody(BodySetupToUse, BodyTransform, this, PhysScene, SpawnParams);
	}
}

void UEdenRigidRopeComponent::InitRopeConstraints()
{
	if (!RopeAsset)
	{
		return;
	}

	const int32 NumSegs = RopeAsset->NumSegments;
	const int32 NumConstraintCount = NumSegs - 1;
	if (NumConstraintCount <= 0)
	{
		return;
	}

	Constraints.Reset();
	Constraints.AddZeroed(NumConstraintCount);

	const float SegmentLength = RopeAsset->GetSegmentLength();

	// 从 Asset 读取约束默认参数
	const float AssetSwingLimitAngle = RopeAsset->DefaultSwingLimitAngle;
	const float AssetSwingStiffness = RopeAsset->DefaultSwingStiffness;
	const float AssetSwingDamping = RopeAsset->DefaultSwingDamping;
	const EAngularConstraintMotion AssetTwistMotion = RopeAsset->DefaultTwistMotion;

	UWorld* World = GetWorld();
	FPhysScene* PhysScene = World ? World->GetPhysicsScene() : nullptr;

	for (int32 i = 0; i < NumConstraintCount; i++)
	{
		FBodyInstance* Body1 = Bodies[i + 1];
		FBodyInstance* Body2 = Bodies[i];

		if (!Body1 || !Body2 || !Body1->IsValidBodyInstance() || !Body2->IsValidBodyInstance())
		{
			UE_LOG(LogTemp, Warning, TEXT("EdenRigidRopeComponent: Invalid body instance at constraint %d"), i);
			continue;
		}

		FConstraintInstance* ConInst = new FConstraintInstance();
		Constraints[i] = ConInst;

		// 如果 Asset 中有对应的 ConstraintTemplate，使用其配置
		if (RopeAsset->ConstraintSetup.IsValidIndex(i) && RopeAsset->ConstraintSetup[i])
		{
			const FConstraintProfileProperties& TemplateProfile = RopeAsset->ConstraintSetup[i]->DefaultInstance.ProfileInstance;
			ConInst->ProfileInstance = TemplateProfile;
		}
		else
		{
			// Fallback: 使用 Asset 的默认参数
			FConstraintProfileProperties& Profile = ConInst->ProfileInstance;

			Profile.LinearLimit.XMotion = ELinearConstraintMotion::LCM_Locked;
			Profile.LinearLimit.YMotion = ELinearConstraintMotion::LCM_Locked;
			Profile.LinearLimit.ZMotion = ELinearConstraintMotion::LCM_Locked;

			Profile.ConeLimit.Swing1Motion = EAngularConstraintMotion::ACM_Limited;
			Profile.ConeLimit.Swing1LimitDegrees = AssetSwingLimitAngle;
			Profile.ConeLimit.Swing2Motion = EAngularConstraintMotion::ACM_Limited;
			Profile.ConeLimit.Swing2LimitDegrees = AssetSwingLimitAngle;
			Profile.TwistLimit.TwistMotion = AssetTwistMotion;

			Profile.ConeLimit.bSoftConstraint = true;
			Profile.ConeLimit.Stiffness = AssetSwingStiffness;
			Profile.ConeLimit.Damping = AssetSwingDamping;
		}

		// Body1 = Bodies[i+1] (子 Body)，原点在 Particle[i+1]，即交界处，锚点偏移为 0
		// Body2 = Bodies[i]   (父 Body)，原点在 Particle[i]，沿 +X 偏移 SegmentLength 到达交界处
		ConInst->Pos1 = FVector(0, 0, 0);
		ConInst->Pos2 = FVector(SegmentLength, 0, 0);
		ConInst->PriAxis1 = FVector(-1, 0, 0);
		ConInst->SecAxis1 = FVector(0, -1, 0);
		ConInst->PriAxis2 = FVector(-1, 0, 0);
		ConInst->SecAxis2 = FVector(0, -1, 0);

		// Set physics scene for constraint
		ConInst->PhysScene = PhysScene;

		// Initialize constraint
		ConInst->InitConstraint(Body1, Body2, 1.0f, this, FOnConstraintBroken(), FOnPlasticDeformation());
	}
}

void UEdenRigidRopeComponent::OnCreatePhysicsState()
{
	// Skip UPrimitiveComponent's default implementation since we create multiple bodies
	USceneComponent::OnCreatePhysicsState();

	if (!RopeAsset)
	{
		return;
	}

	UWorld* World = GetWorld();
	FPhysScene* PhysScene = World ? World->GetPhysicsScene() : nullptr;
	if (!PhysScene)
	{
		UE_LOG(LogTemp, Warning, TEXT("EdenRigidRopeComponent: No physics scene available"));
		return;
	}

	// 1. Create BodySetup
	CreateRopeBodySetup();
	if (!RopeBodySetup)
	{
		UE_LOG(LogTemp, Warning, TEXT("EdenRigidRopeComponent: Failed to create BodySetup"));
		return;
	}

	// 2. Create Aggregate (NOTE: no-op in Chaos; self-collision is handled by SetupSelfCollisionDisable below)
	const int32 NumSegs = RopeAsset->NumSegments;
	if (!Aggregate.IsValid())
	{
		Aggregate = FPhysicsInterface::CreateAggregate(NumSegs);
	}

	// 3. Initialize Bodies (always created as kinematic, simulation enabled separately)
	InitRopeBodies(PhysScene);

	// 4. Add Aggregate to scene
	if (Aggregate.IsValid())
	{
		PhysScene->AddAggregateToScene(Aggregate);
	}

	// 5. Initialize internal Constraints
	if (bDebugUseConstraints)
	{
		InitRopeConstraints();
	}

	// 6. Enable simulation only when requested
	// Bodies are created as kinematic (bSimulatePhysics=false in InitRopeBodies),
	// so they won't fly apart. Only enable simulation after constraints are in place.
	if (BodyInstance.bSimulatePhysics)
	{
		for (FBodyInstance* BodyInst : Bodies)
		{
			if (BodyInst && BodyInst->IsValidBodyInstance())
			{
				BodyInst->SetInstanceSimulatePhysics(true, false, true);
			}
		}
	}

	// 7. Disable self-collision between this rope's own bodies (unless explicitly enabled).
	// Done after bodies exist and have valid physics actors.
	SetupSelfCollisionDisable();

	// 8. Re-initialize any external PhysicsConstraintComponents that tried to
	// bind before our bodies existed (fixes initialization order race condition)
	ReinitPendingExternalConstraints();

	// 9. 物理体创建完成后更新 ParticleSpaceTransforms 缓存
	UpdateParticleSpaceTransforms();

	// 10. 初始化 TransformToRoot：Component 相对于 RootBody (Bodies[0]) 的固定偏移
	// 参考 SKM 的 SetRootBodyIndex()，一次性计算后在物理模拟期间保持不变
	if (Bodies.Num() > 0 && Bodies[0] && Bodies[0]->IsValidBodyInstance())
	{
		TransformToRoot = GetComponentTransform().GetRelativeTransform(Bodies[0]->GetUnrealWorldTransform());
	}
	else
	{
		TransformToRoot = FTransform::Identity;
	}

	UE_LOG(LogTemp, Log, TEXT("EdenRigidRopeComponent: Created %d bodies and %d constraints (Simulating: %s)"),
		Bodies.Num(), Constraints.Num(), BodyInstance.bSimulatePhysics ? TEXT("Yes") : TEXT("No"));
}

void UEdenRigidRopeComponent::OnDestroyPhysicsState()
{
	// Terminate external constraints BEFORE destroying our bodies,
	// so they don't hold dangling FBodyInstance pointers.
	NotifyExternalConstraintsAboutDestruction();

	TermRopePhysics();
	USceneComponent::OnDestroyPhysicsState();
}

void UEdenRigidRopeComponent::TermRopePhysics()
{
	// Remove self-collision ignore relationships while body handles are still valid
	TeardownSelfCollisionDisable();

	// First destroy constraints
	for (FConstraintInstance* Constraint : Constraints)
	{
		if (Constraint)
		{
			Constraint->TermConstraint();
			delete Constraint;
		}
	}
	Constraints.Empty();

	// Then destroy bodies
	for (FBodyInstance* Body : Bodies)
	{
		if (Body)
		{
			Body->TermBody();
			delete Body;
		}
	}
	Bodies.Empty();

	// Release Aggregate
	if (Aggregate.IsValid())
	{
		FPhysicsInterface::ReleaseAggregate(Aggregate);
	}

	// BodySetup is a UObject, will be cleaned up by GC
	RopeBodySetup = nullptr;
}

void UEdenRigidRopeComponent::SetupSelfCollisionDisable()
{
	// 已启用自碰撞则无需禁用；已经登记过也直接返回，避免重复
	if (!RopeAsset || bEnableSelfCollision || bSelfCollisionDisabled)
	{
		return;
	}

	// 收集所有有效的物理 actor handle
	TArray<FPhysicsActorHandle> Handles;
	Handles.Reserve(Bodies.Num());
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			if (FPhysicsActorHandle Handle = EdenGetBodyPhysicsActor(BI))
			{
				Handles.Add(Handle);
			}
		}
	}

	if (Handles.Num() < 2)
	{
		return; // 少于两个 body 无需禁用自碰撞
	}

	// 为每一对 body 建立忽略关系。IgnoreCollisionManager 内部会自动做成双向
	// （见 FIgnoreCollisionManager::AddIgnoreCollisions），因此每对只需登记一次。
	// 做法与 USkeletalMeshComponent::InitCollisionRelationships 一致。
	TMap<FPhysicsActorHandle, TArray<FPhysicsActorHandle>> DisabledCollisions;
	for (int32 i = 0; i < Handles.Num() - 1; ++i)
	{
		TArray<FPhysicsActorHandle>& Targets = DisabledCollisions.Add(Handles[i], TArray<FPhysicsActorHandle>());
		Targets.Reserve(Handles.Num() - i - 1);
		for (int32 j = i + 1; j < Handles.Num(); ++j)
		{
			Targets.Add(Handles[j]);
		}
	}

	UWorld* World = GetWorld();
	FPhysScene* PhysScene = World ? World->GetPhysicsScene() : nullptr;
	if (!PhysScene)
	{
		return;
	}

	FPhysicsCommand::ExecuteWrite(PhysScene, [&DisabledCollisions]()
	{
		FChaosEngineInterface::AddDisabledCollisionsFor_AssumesLocked(DisabledCollisions);
	});

	bSelfCollisionDisabled = true;
}

void UEdenRigidRopeComponent::TeardownSelfCollisionDisable()
{
	if (!bSelfCollisionDisabled)
	{
		return;
	}

	TArray<FPhysicsActorHandle> Handles;
	Handles.Reserve(Bodies.Num());
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			if (FPhysicsActorHandle Handle = EdenGetBodyPhysicsActor(BI))
			{
				Handles.Add(Handle);
			}
		}
	}

	if (Handles.Num() > 0)
	{
		UWorld* World = GetWorld();
		FPhysScene* PhysScene = World ? World->GetPhysicsScene() : nullptr;
		if (PhysScene)
		{
			FPhysicsCommand::ExecuteWrite(PhysScene, [&Handles]()
			{
				FChaosEngineInterface::RemoveDisabledCollisionsFor_AssumesLocked(Handles);
			});
		}
	}

	bSelfCollisionDisabled = false;
}

void UEdenRigidRopeComponent::ReinitPendingExternalConstraints()
{
	AActor* Owner = GetOwner();
	if (!Owner || Bodies.Num() == 0)
	{
		return;
	}

	TInlineComponentArray<UPhysicsConstraintComponent*> ExtConstraints;
	Owner->GetComponents(ExtConstraints);

	for (UPhysicsConstraintComponent* CC : ExtConstraints)
	{
		if (!CC || !CC->IsRegistered())
		{
			continue;
		}

		// Check if this constraint references us on either frame
		UPrimitiveComponent* OutComp1 = nullptr;
		UPrimitiveComponent* OutComp2 = nullptr;
		FName OutBone1, OutBone2;
		CC->GetConstrainedComponents(OutComp1, OutBone1, OutComp2, OutBone2);

		const bool bReferencesUs = (OutComp1 == this || OutComp2 == this);

		if (bReferencesUs && !CC->ConstraintInstance.IsValidConstraintInstance())
		{
			// The constraint tried to init before our bodies existed.
			// Re-trigger its initialization now.
			UE_LOG(LogTemp, Verbose,
				TEXT("EdenRigidRopeComponent %s: Re-initializing external constraint %s"),
				*GetName(), *CC->GetName());
			CC->InitComponentConstraint();
		}
	}
}

void UEdenRigidRopeComponent::NotifyExternalConstraintsAboutDestruction()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TInlineComponentArray<UPhysicsConstraintComponent*> ExtConstraints;
	Owner->GetComponents(ExtConstraints);

	for (UPhysicsConstraintComponent* CC : ExtConstraints)
	{
		if (!CC)
		{
			continue;
		}

		UPrimitiveComponent* OutComp1 = nullptr;
		UPrimitiveComponent* OutComp2 = nullptr;
		FName OutBone1, OutBone2;
		CC->GetConstrainedComponents(OutComp1, OutBone1, OutComp2, OutBone2);

		if (OutComp1 == this || OutComp2 == this)
		{
			CC->TermComponentConstraint();
		}
	}
}

void UEdenRigidRopeComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	// 参考 USkeletalMeshComponent::OnUpdateTransform 的模式：
	// 跳过基类的单体物理更新（SkipPhysicsUpdate），自行处理多物理体的同步
	Super::OnUpdateTransform(UpdateTransformFlags | EUpdateTransformFlags::SkipPhysicsUpdate, Teleport);

	// 如果没有粒子数据或组件空间缓存，无需自定义处理
	if (Particles.Num() == 0 || ParticleSpaceTransforms.Num() == 0)
	{
		return;
	}

	// 仅在非 SkipPhysicsUpdate 时处理。
	// SkipPhysicsUpdate 由 SyncComponentToRBPhysics 触发（物理同步路径），
	// 此时 Particle 已由 SyncParticlesFromBodies 更新，跳过避免双重偏移。
	if (!(UpdateTransformFlags & EUpdateTransformFlags::SkipPhysicsUpdate))
	{
		const FTransform& CompTransform = GetComponentTransform();

		// 从组件空间缓存（ParticleSpaceTransforms）× 新的 ComponentToWorld 重算所有 Particle 的世界空间位置。
		// 这是无状态、幂等的设计：不依赖任何 "Previous" 缓存，不使用增量变换。
		// 与 SKM 的 UpdateKinematicBonesToAnim(GetComponentSpaceTransforms()) 模式一致。
		const int32 Count = FMath::Min(Particles.Num(), ParticleSpaceTransforms.Num());
		for (int32 i = 0; i < Count; ++i)
		{
			Particles[i].OldPosition = Particles[i].Position;
			Particles[i].Position = CompTransform.TransformPosition(ParticleSpaceTransforms[i].GetTranslation());
		}

		// 如果物理状态已创建，同步所有物理体
		if (bPhysicsStateCreated)
		{
			TeleportAllBodies(Teleport);
		}

		// 更新 TransformToRoot：Component 与 RootBody 的相对关系
		// 仅在非 SkipPhysicsUpdate 时更新，避免与 SyncComponentToRBPhysics 循环依赖
		if (Bodies.Num() > 0 && Bodies[0] && Bodies[0]->IsValidBodyInstance())
		{
			TransformToRoot = CompTransform.GetRelativeTransform(Bodies[0]->GetUnrealWorldTransform());
		}
	}

	// Particle 位置变更后更新 ParticleSpaceTransforms 缓存
	UpdateParticleSpaceTransforms();

	// 触发渲染更新，确保视觉表现立即反映新的粒子位置
	MarkRenderDynamicDataDirty();
}

void UEdenRigidRopeComponent::TeleportAllBodies(ETeleportType Teleport)
{
	if (Bodies.Num() == 0 || Particles.Num() < 2)
	{
		return;
	}

	// 参考 SkeletalMeshComponent::UpdateKinematicBonesToAnim 的逐 body 判断模式：
	// - kinematic body (!IsInstanceSimulatingPhysics()) → 总是更新
	// - simulating body + teleport → 也更新（强制瞬移）
	// - simulating body + 非 teleport → 跳过（由物理引擎驱动）
	const bool bTeleport = (Teleport == ETeleportType::TeleportPhysics);

	for (int32 i = 0; i < Bodies.Num() && i + 1 < Particles.Num(); i++)
	{
		FBodyInstance* BodyInst = Bodies[i];
		if (BodyInst && BodyInst->IsValidBodyInstance())
		{
			if (bTeleport || !BodyInst->IsInstanceSimulatingPhysics())
			{
				// Body 原点位于 Particle 端点（与 SkeletalMesh 的 Bone-Body 模式一致）
				const FVector& StartPos = Particles[i].Position;
				const FVector& EndPos = Particles[i + 1].Position;
				const FVector Direction = (EndPos - StartPos).GetSafeNormal();
				const FQuat Rotation = FQuat::FindBetweenNormals(FVector::ForwardVector, Direction);

				BodyInst->SetBodyTransform(FTransform(Rotation, StartPos), Teleport);
			}
		}
	}
}

bool UEdenRigidRopeComponent::IsAnyBodySimulatingPhysics() const
{
	for (FBodyInstance* BodyInst : Bodies)
	{
		if (BodyInst && BodyInst->IsValidBodyInstance() && BodyInst->IsInstanceSimulatingPhysics())
		{
			return true;
		}
	}
	return false;
}

void UEdenRigidRopeComponent::SyncParticlesFromBodies()
{
	if (Bodies.Num() == 0 || !RopeAsset)
	{
		return;
	}

	const float SegmentLength = RopeAsset->GetSegmentLength();

	for (int32 i = 0; i < Bodies.Num(); i++)
	{
		FBodyInstance* Body = Bodies[i];
		if (Body && Body->IsValidBodyInstance())
		{
			FTransform BodyTM = Body->GetUnrealWorldTransform();

			// Body 原点即 Particle 位置（Particle 空间 == BodyInstance 空间）
			Particles[i].OldPosition = Particles[i].Position;
			Particles[i].Position = BodyTM.GetLocation();
			
			// 最后一个 Body 还需更新末尾 Particle（沿 Forward 偏移整个 SegmentLength）
			if (i == Bodies.Num() - 1)
			{
				FVector BodyForward = BodyTM.GetRotation().GetForwardVector();
				Particles[i + 1].OldPosition = Particles[i + 1].Position;
			Particles[i + 1].Position = BodyTM.GetLocation() + BodyForward * SegmentLength;
			}
		}
	}

	// 物理同步后更新 ParticleSpaceTransforms 缓存
	UpdateParticleSpaceTransforms();
}

void UEdenRigidRopeComponent::UpdateParticleSpaceTransforms()
{
	const int32 NumParticlesCount = Particles.Num();
	if (NumParticlesCount < 2)
	{
		ParticleSpaceTransforms.Reset();
		return;
	}

	ParticleSpaceTransforms.SetNum(NumParticlesCount);

	const FTransform ComponentTransformInverse = GetComponentTransform().Inverse();

	for (int32 i = 0; i < NumParticlesCount; ++i)
	{
		// 计算 Forward 方向（与 Body 旋转计算方式一致）
		FVector Forward;
		if (i < NumParticlesCount - 1)
		{
			Forward = (Particles[i + 1].Position - Particles[i].Position).GetSafeNormal();
		}
		else
		{
			Forward = (Particles[i].Position - Particles[i - 1].Position).GetSafeNormal();
		}

		// 构建世界空间变换（旋转方式与 InitRopeBodies / SyncParticlesFromBodies 中一致）
		const FQuat WorldRot = FQuat::FindBetweenNormals(FVector::ForwardVector, Forward);
		const FTransform WorldTM(WorldRot, Particles[i].Position);

		// 转换到 Component Space
		ParticleSpaceTransforms[i] = WorldTM * ComponentTransformInverse;
	}
}

void UEdenRigidRopeComponent::UpdateEndPhysicsTickRegisteredState()
{
	RegisterEndPhysicsTick(PrimaryComponentTick.IsTickFunctionRegistered() && ShouldRunEndPhysicsTick());
}

void UEdenRigidRopeComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);

	UpdateEndPhysicsTickRegisteredState();
}

void UEdenRigidRopeComponent::RegisterEndPhysicsTick(bool bRegister)
{
	if (bRegister != EndPhysicsTickFunction.IsTickFunctionRegistered())
	{
		if (bRegister)
		{
			UWorld* World = GetWorld();
			if (World->EndPhysicsTickFunction.IsTickFunctionRegistered() && SetupActorComponentTickFunction(&EndPhysicsTickFunction))
			{
				EndPhysicsTickFunction.Target = this;
				// Make sure our EndPhysicsTick gets called after physics simulation is finished
				if (World != nullptr)
				{
					EndPhysicsTickFunction.AddPrerequisite(World, World->EndPhysicsTickFunction);
				}
			}
		}
		else
		{
			EndPhysicsTickFunction.UnRegisterTickFunction();
		}
	}
}

bool UEdenRigidRopeComponent::ShouldRunEndPhysicsTick() const
{
	// Run EndPhysicsTick when any body is actually simulating physics (not just configured to)
	return IsAnyBodySimulatingPhysics() && IsSimulatingPhysics() && RigidBodyIsAwake();
}

void UEdenRigidRopeComponent::EndPhysicsTickComponent(FEdenRigidRopeEndPhysicsTickFunction& ThisTickFunction)
{
	// Sync particle positions from physics bodies after simulation completes
	// 使用实际运行状态而非配置值，确保仅在 body 真正模拟时才同步
	if (IsAnyBodySimulatingPhysics())
	{
		SyncParticlesFromBodies();
		
		// Sync component transform to the first body (root body)
		// This makes the component's root position follow the rope's start point
		// Similar to how SkeletalMeshComponent syncs to its root body
		if (IsRegistered() && RigidBodyIsAwake())
		{
			SyncComponentToRBPhysics();
		}
	}

	// Mark render data as dirty to update visuals
	MarkRenderDynamicDataDirty();

	// SegmentMesh 模式：物理同步后立刻刷新 ISMC 实例变换，
	// 保证 mesh 与 Bodies/Particles 在同一帧内一致
	if (ShouldUseSegmentMeshRender() && SegmentMeshISMC)
	{
		UpdateSegmentMeshInstances();
	}

	// Update bounds
	UpdateComponentToWorld();
}

// Called when the game starts
void UEdenRigidRopeComponent::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void UEdenRigidRopeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                      FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Log particle positions BEFORE update
	// {
	// 	const uint64 FrameNumber = GFrameCounter;
	// 	UE_LOG(LogTemp, Display, TEXT("[Frame %llu] %s TickComponent - BEFORE update:"), FrameNumber, *GetName());
	// 	for (int32 i = 0; i < Particles.Num(); i++)
	// 	{
	// 		UE_LOG(LogTemp, Display, TEXT("  Particle[%d]: Position=(%s)"), i, *Particles[i].Position.ToString());
	// 	}
	// }
	
	UpdateEndPhysicsTickRegisteredState();

	// Physics simulation mode: sync is handled in EndPhysicsTickComponent (after physics step)
	// Non-physics mode: maintain original straight line following logic
	// 使用实际运行状态（IsAnyBodySimulatingPhysics）而非配置值（BodyInstance.bSimulatePhysics），
	// 因为在编辑器中即使 bSimulatePhysics=true，bodies 实际上仍是 kinematic 的
	if (!IsAnyBodySimulatingPhysics())
	{
		if (!RopeAsset)
		{
			return; // 没有 Asset，无法更新粒子
		}

		FVector RopeStart, RopeEnd;
		GetEndPositions(RopeStart, RopeEnd);

		const FVector Delta = RopeEnd - RopeStart;
		const int32 NumSegs = RopeAsset->NumSegments;
		const int32 NumParticles = NumSegs + 1;

		for(int32 ParticleIdx = 0; ParticleIdx < NumParticles && ParticleIdx < Particles.Num(); ParticleIdx++)
		{
			FEdenRigidRopeParticle& Particle = Particles[ParticleIdx];

			const float Alpha = (float)ParticleIdx / (float)NumSegs;
			const FVector NewPosition = RopeStart + (Alpha * Delta);

			Particle.OldPosition = Particle.Position;
			Particle.Position = NewPosition;
		}

		// Sync kinematic bodies to follow particle positions,
		// so external PhysicsConstraints have up-to-date transforms.
		if (Bodies.Num() > 0)
		{
			for (int32 i = 0; i < Bodies.Num() && i + 1 < Particles.Num(); i++)
			{
				if (Bodies[i] && Bodies[i]->IsValidBodyInstance())
				{
					// Body 原点位于 Particle 端点（与 BodyInstance 空间一致）
					const FVector Dir = (Particles[i + 1].Position - Particles[i].Position).GetSafeNormal();
					const FQuat Rot = FQuat::FindBetweenNormals(FVector::ForwardVector, Dir);
					Bodies[i]->SetBodyTransform(FTransform(Rot, Particles[i].Position), ETeleportType::TeleportPhysics);
				}
			}
		}

		// 非物理模式下 Particle 位置更新后刷新 ParticleSpaceTransforms 缓存
		UpdateParticleSpaceTransforms();

		// Need to send new data to render thread (physics mode does this in EndPhysicsTick)
		MarkRenderDynamicDataDirty();

		// SegmentMesh 模式：非模拟路径下同步更新 ISMC 实例变换
		if (ShouldUseSegmentMeshRender() && SegmentMeshISMC)
		{
			UpdateSegmentMeshInstances();
		}

		// Call this because bounds have changed
		UpdateComponentToWorld();
	}

	// Log particle positions AFTER update
	// {
	// 	const uint64 FrameNumber = GFrameCounter;
	// 	UE_LOG(LogTemp, Display, TEXT("[Frame %llu] %s TickComponent - AFTER update:"), FrameNumber, *GetName());
	// 	for (int32 i = 0; i < Particles.Num(); i++)
	// 	{
	// 		UE_LOG(LogTemp, Display, TEXT("  Particle[%d]: Position=(%s)"), i, *Particles[i].Position.ToString());
	// 	}
	// }
}

// ============================================================================
// Segment Mesh Rendering
// 语义与 UEdenRopeComponentBase::UpdateSegmentMeshInstances 保持一致，
// 数据源为 Particles（与 SendRenderDynamicData_Concurrent 同源），
// 避免物理 kinematic target 延迟带来的帧间抖动。
// ============================================================================

bool UEdenRigidRopeComponent::ShouldUseSegmentMeshRender() const
{
	return RopeAsset
		&& RopeAsset->RenderMode == ERopeRenderMode::SegmentMesh
		&& RopeAsset->SegmentMesh != nullptr;
}

void UEdenRigidRopeComponent::CreateSegmentMeshISMC()
{
	if (!ShouldUseSegmentMeshRender() || SegmentMeshISMC)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	SegmentMeshISMC = NewObject<UInstancedStaticMeshComponent>(Owner,
		UInstancedStaticMeshComponent::StaticClass(), NAME_None, RF_Transient);
	SegmentMeshISMC->SetStaticMesh(RopeAsset->SegmentMesh);
	SegmentMeshISMC->SetMobility(EComponentMobility::Movable);
	SegmentMeshISMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SegmentMeshISMC->SetupAttachment(this);
	SegmentMeshISMC->RegisterComponent();

	// 预填 Identity 实例，数量 = NumSegments（= Particles.Num() - 1 when particles ready）
	const int32 NumSegments = FMath::Max(0, Particles.Num() > 1
		? Particles.Num() - 1
		: RopeAsset->NumSegments);
	for (int32 i = 0; i < NumSegments; ++i)
	{
		SegmentMeshISMC->AddInstance(FTransform::Identity);
	}

	UpdateSegmentMeshInstances();
}

void UEdenRigidRopeComponent::DestroySegmentMeshISMC()
{
	if (SegmentMeshISMC)
	{
		SegmentMeshISMC->DestroyComponent();
		SegmentMeshISMC = nullptr;
	}
}

void UEdenRigidRopeComponent::UpdateSegmentMeshInstances()
{
	// 需求 7.1：Particles 不足直接早退
	if (!SegmentMeshISMC || !RopeAsset || Particles.Num() < 2)
	{
		return;
	}

	const int32 NumSegments = Particles.Num() - 1;

	// 需求 7.2：ISMC 实例数与目标不一致则自动对齐
	if (SegmentMeshISMC->GetInstanceCount() != NumSegments)
	{
		SegmentMeshISMC->ClearInstances();
		for (int32 i = 0; i < NumSegments; ++i)
		{
			SegmentMeshISMC->AddInstance(FTransform::Identity);
		}
	}

	const float RefLength = GetSegmentMeshRefLength();
	const int32 FwdIdx    = GetSegmentMeshForwardAxisIndex();
	const FVector FwdVec  = GetSegmentMeshForwardAxisVector();

	const float TwistAngleDeg   = RopeAsset->SegmentMeshTwistAngle;
	const FVector2D CrossScale  = RopeAsset->SegmentMeshCrossScale;
	const float ForwardScale    = RopeAsset->SegmentMeshForwardScale;

	TArray<FTransform> Transforms;
	Transforms.Reserve(NumSegments);

	for (int32 i = 0; i < NumSegments; ++i)
	{
		const FVector& P0  = Particles[i].Position;
		const FVector& P1  = Particles[i + 1].Position;
		const FVector Dir  = P1 - P0;
		const float SegLen = Dir.Size();

		// 需求 7.4：方向退化时回退为 FwdVec，Rotation 为 Identity，不产生 NaN
		const FVector DirN = SegLen > KINDA_SMALL_NUMBER ? Dir / SegLen : FwdVec;
		const FVector Mid  = (P0 + P1) * 0.5f;

		FQuat Rot = FQuat::FindBetweenNormals(FwdVec, DirN);
		if ((i % 2) == 1 && !FMath::IsNearlyZero(TwistAngleDeg))
		{
			const FQuat TwistRot(FwdVec, FMath::DegreesToRadians(TwistAngleDeg));
			Rot = Rot * TwistRot;
		}

		FVector Scale(1.0f);
		// 需求 7.3：RefLength 退化时前向轴回退为 1.0 * ForwardScale
		Scale[FwdIdx]              = (RefLength > KINDA_SMALL_NUMBER ? SegLen / RefLength : 1.0f) * ForwardScale;
		Scale[(FwdIdx + 1) % 3]    = CrossScale.X;
		Scale[(FwdIdx + 2) % 3]    = CrossScale.Y;

		Transforms.Add(FTransform(Rot, Mid, Scale));
	}

	SegmentMeshISMC->BatchUpdateInstancesTransforms(0, Transforms,
		/*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
}

float UEdenRigidRopeComponent::GetSegmentMeshRefLength() const
{
	if (!RopeAsset || !RopeAsset->SegmentMesh)
	{
		return 1.0f;
	}
	const FBoxSphereBounds MeshBounds = RopeAsset->SegmentMesh->GetBounds();
	const float RefLen = MeshBounds.BoxExtent[GetSegmentMeshForwardAxisIndex()] * 2.0f;
	return RefLen > KINDA_SMALL_NUMBER ? RefLen : 1.0f;
}

int32 UEdenRigidRopeComponent::GetSegmentMeshForwardAxisIndex() const
{
	if (!RopeAsset)
	{
		return 0;
	}
	switch (RopeAsset->SegmentMeshForwardAxis)
	{
		case ESegmentMeshForwardAxis::X: return 0;
		case ESegmentMeshForwardAxis::Y: return 1;
		case ESegmentMeshForwardAxis::Z: return 2;
		default:                          return 0;
	}
}

FVector UEdenRigidRopeComponent::GetSegmentMeshForwardAxisVector() const
{
	if (!RopeAsset)
	{
		return FVector::XAxisVector;
	}
	switch (RopeAsset->SegmentMeshForwardAxis)
	{
		case ESegmentMeshForwardAxis::X: return FVector::XAxisVector;
		case ESegmentMeshForwardAxis::Y: return FVector::YAxisVector;
		case ESegmentMeshForwardAxis::Z: return FVector::ZAxisVector;
		default:                          return FVector::XAxisVector;
	}
}
