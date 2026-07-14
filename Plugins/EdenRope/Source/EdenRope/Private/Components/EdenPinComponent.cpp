// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/EdenPinComponent.h"

#include "Components/EdenRopeComponentBase.h"
#include "Components/EdenRopeComponent.h"
#include "Macro/EdenRopeMacroDefine.h"
#include "Solver/EdenRopeWorldSubsystem.h"
#include "Solver/EdenRopeSolverActor.h"
#include "Components/PrimitiveComponent.h"

UEdenPinComponent::UEdenPinComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	SetTickGroup(TG_PrePhysics);

#if WITH_EDITORONLY_DATA
	// 允许在编辑器中可视化组件
	bVisualizeComponent = true;
#endif
}

void UEdenPinComponent::BeginPlay()
{
	Super::BeginPlay();

	// 用 Base 类 getter 同时支持 Rope 和 Rod
	UEdenRopeComponentBase* RopeBase = GetPinnedRopeComponentBase();
	if (RopeBase)
	{
		if (RopeBase->IsSolverRegistered())
		{
			RegisterPinConstraint(RopeBase->GetRopeHandle());
		}
		else
		{
			// 绑定 OnRopeRegistered 委托，等待绳索/杆注册后再注册 Pin 约束
			RopeBase->OnRopeRegistered.AddUObject(this, &UEdenPinComponent::RegisterPinConstraint);
		}
	}
}

void UEdenPinComponent::RegisterPinConstraint(const FEdenRopeHandle& RopeHandle)
{
	if (!GetPinnedRopeComponentBase())
	{
		UE_LOG(LogEdenRope, Warning, TEXT("[URopePinComponent::RegisterPinConstraint] No valid rope/rod component found"));
		return;
	}

	UEdenRopeWorldSubsystem* Subsystem = UEdenRopeWorldSubsystem::Get(GetWorld());
	if (!Subsystem) return;

	AEdenRopeSolverActor* Solver = Subsystem->GetSolver();
	if (!Solver) return;

	if (!RopeHandle.IsValid())
	{
		UE_LOG(LogEdenRope, Warning, TEXT("[URopePinComponent::RegisterPinConstraint] Rope handle is invalid"));
		return;
	}

	if (PinnedParticleIndex < 0 || PinnedParticleIndex >= RopeHandle.ParticleCount)
	{
		UE_LOG(LogEdenRope, Warning, TEXT("[URopePinComponent::RegisterPinConstraint] Invalid particle index %d (rope has %d particles)"),
			PinnedParticleIndex, RopeHandle.ParticleCount);
		return;
	}

	const int32 GlobalParticleIndex = RopeHandle.ToGlobalIndex(PinnedParticleIndex);

	// 检测是否为 Rod（含段方向数据）
	bOwnerIsRod = (RopeHandle.SegmentOrientationStartIndex != INDEX_NONE && RopeHandle.SegmentOrientationCount > 0);
	bHasOrientationConstraint = false;

	// 根据连接模式选择注册方式
	if (AttachmentMode == ERopePinAttachmentMode::AttachToComponent)
	{
		UPrimitiveComponent* AttachedComp = GetAttachedPrimitiveComponent();
		if (AttachedComp)
		{
			PinConstraintHandle = Solver->RegisterPinConstraintToComponent(GlobalParticleIndex, AttachedComp, LocalOffset);
			bRegisteredAsWorldPosition = false;
			UE_LOG(LogEdenRope, Log, TEXT("[URopePinComponent::RegisterPinConstraint] Registered pin constraint to component %s with offset (%s)"),
				*AttachedComp->GetName(), *LocalOffset.ToString());
		}
		else
		{
			UE_LOG(LogEdenRope, Warning, TEXT("[URopePinComponent::RegisterPinConstraint] AttachToComponent mode but no valid PrimitiveComponent found, falling back to WorldPosition"));
			PinConstraintHandle = Solver->RegisterPinConstraint(GlobalParticleIndex, GetComponentLocation());
			bRegisteredAsWorldPosition = true;
		}
	}
	else
	{
		// WorldPosition 模式
		if (bOwnerIsRod && bConstrainOrientation)
		{
			// Rod + 旋转约束：粒子 i → segment clamp(i, 0, SegCount-1)
			const int32 LocalSegIdx  = FMath::Clamp(PinnedParticleIndex, 0, RopeHandle.SegmentOrientationCount - 1);
			const int32 GlobalSegIdx = RopeHandle.SegmentOrientationStartIndex + LocalSegIdx;

			const FQuat TargetOrientation = GetComponentQuat();
			const FQuat RestDarboux(0.f, 0.f, 0.f, 0.f); // Zero = force q_seg -> q_target

			PinConstraintHandle = Solver->RegisterPinConstraintWithOrientation(
				GlobalParticleIndex, GetComponentLocation(),
				GlobalSegIdx, TargetOrientation, RestDarboux);
			bRegisteredAsWorldPosition = true;
			bHasOrientationConstraint  = true;

			UE_LOG(LogEdenRope, Log, TEXT("[URopePinComponent::RegisterPinConstraint] Rod pin with orientation: particle=%d, seg=%d"),
				GlobalParticleIndex, GlobalSegIdx);
		}
		else
		{
			// Rope 或 Rod 不约束旋转：仅位置约束
			PinConstraintHandle = Solver->RegisterPinConstraint(GlobalParticleIndex, GetComponentLocation());
			bRegisteredAsWorldPosition = true;
		}
	}
}

void UEdenPinComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                      FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!PinConstraintHandle.IsValid())
	{
		return;
	}

	// 只有实际注册为 WorldPosition 时才需要每帧更新位置
	// AttachToComponent 成功附着时：位置由 Evolution 从 DynamicRigidbody 中自动计算
	// AttachToComponent 回落为 WorldPosition 时：bRegisteredAsWorldPosition = true，需要 Tick 更新
	if (!bRegisteredAsWorldPosition)
	{
		return;
	}

	// WorldPosition 模式：需要每帧更新位置
	UEdenRopeWorldSubsystem* Subsystem = UEdenRopeWorldSubsystem::Get(GetWorld());
	if (!Subsystem) return;

	AEdenRopeSolverActor* Solver = Subsystem->GetSolver();
	if (!Solver) return;

	Solver->UpdatePinConstraintPosition(PinConstraintHandle, GetComponentLocation());

	// 如果有旋转约束，同步更新目标朝向
	if (bHasOrientationConstraint)
	{
		Solver->UpdatePinConstraintOrientation(PinConstraintHandle, GetComponentQuat());
	}
}

UEdenRopeComponentBase* UEdenPinComponent::GetPinnedRopeComponentBase() const
{
	// 确定要搜索的 Actor
	AActor* TargetActor = PinnedRopeActor;

	// 如果没有指定 Actor，但指定了组件名，使用 Owner
	if (TargetActor == nullptr && PinnedRopeComponentName != NAME_None)
	{
		TargetActor = GetOwner();
	}

	// 如果仍然没有 Actor，返回 nullptr
	if (TargetActor == nullptr)
	{
		return nullptr;
	}

	// 如果指定了组件名，按名称查找
	if (PinnedRopeComponentName != NAME_None)
	{
		for (UActorComponent* Comp : TargetActor->GetComponents())
		{
			if (Comp->GetFName() == PinnedRopeComponentName)
			{
				return Cast<UEdenRopeComponentBase>(Comp);
			}
		}
		return nullptr;
	}

	// 没有指定组件名，查找第一个 EdenRopeComponentBase（含 Rope 和 Rod）
	return TargetActor->FindComponentByClass<UEdenRopeComponentBase>();
}

UEdenRopeComponent* UEdenPinComponent::GetPinnedRopeComponent() const
{
	// 复用 Base 版本，再向下 Cast 到 Rope 具体类型
	return Cast<UEdenRopeComponent>(GetPinnedRopeComponentBase());
}

UPrimitiveComponent* UEdenPinComponent::GetAttachedPrimitiveComponent() const
{
	// 优先使用运行时设置的组件（参考 PhysicsConstraintComponent::GetComponentInternal）
	if (OverrideAttachedComponent.IsValid())
	{
		return OverrideAttachedComponent.Get();
	}

	// 确定要搜索的 Actor
	AActor* TargetActor = AttachedActor;

	// 如果没有指定 Actor，使用 Owner
	if (TargetActor == nullptr)
	{
		TargetActor = GetOwner();
	}

	// 如果仍然没有 Actor，返回 nullptr
	if (TargetActor == nullptr)
	{
		return nullptr;
	}

	// 如果指定了组件名，按名称查找
	if (AttachedComponentName != NAME_None)
	{
		for (UActorComponent* Comp : TargetActor->GetComponents())
		{
			if (Comp->GetFName() == AttachedComponentName)
			{
				return Cast<UPrimitiveComponent>(Comp);
			}
		}
		return nullptr;
	}

	// 没有指定组件名，查找第一个有物理的 PrimitiveComponent
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	TargetActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	
	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp && PrimComp->GetBodyInstance())
		{
			return PrimComp;
		}
	}

	return nullptr;
}

void UEdenPinComponent::SetAttachedComponent(UPrimitiveComponent* InComponent, FVector InLocalOffset)
{
	if (InComponent)
	{
		AttachedComponentName = InComponent->GetFName();
		OverrideAttachedComponent = InComponent;
		LocalOffset = InLocalOffset;
		AttachmentMode = ERopePinAttachmentMode::AttachToComponent;
	}
}
void UEdenPinComponent::UpdateLocalOffsetFromWorldPosition()
{
#if WITH_EDITOR
	// 防止递归调用
	if (bIsUpdatingLocalOffset)
	{
		return;
	}
#endif

	// 只有在 AttachToComponent 模式下才需要更新 LocalOffset
	if (AttachmentMode != ERopePinAttachmentMode::AttachToComponent)
	{
		return;
	}

	UPrimitiveComponent* AttachedComp = GetAttachedPrimitiveComponent();
	if (!AttachedComp)
	{
		return;
	}

	// 获取 Attached 组件的变换（参考 PhysicsConstraintComponent::UpdateConstraintFrames）
	FTransform AttachedTransform = AttachedComp->GetComponentTransform();
	AttachedTransform.RemoveScaling();

	// 将当前 Pin 的世界位置转换为相对于 Attached 组件的局部偏移
	const FVector WorldPos = GetComponentLocation();
	const FVector NewLocalOffset = AttachedTransform.InverseTransformPosition(WorldPos);

	// 只有在值实际改变时才更新，并通知编辑器（参考 UE 中 PreEditChange/PostEditChange 模式）
	if (!LocalOffset.Equals(NewLocalOffset, KINDA_SMALL_NUMBER))
	{
#if WITH_EDITOR
		// 设置递归防护
		bIsUpdatingLocalOffset = true;

		// 通知编辑器属性即将改变
		FProperty* LocalOffsetProperty = FindFProperty<FProperty>(GetClass(), GET_MEMBER_NAME_CHECKED(UEdenPinComponent, LocalOffset));
		PreEditChange(LocalOffsetProperty);
#endif

		LocalOffset = NewLocalOffset;

#if WITH_EDITOR
		// 通知编辑器属性已改变
		FPropertyChangedEvent PropertyChangedEvent(LocalOffsetProperty);
		PostEditChangeProperty(PropertyChangedEvent);

		// 清除递归防护
		bIsUpdatingLocalOffset = false;
#endif
	}
}
#if WITH_EDITOR
void UEdenPinComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 参考 PhysicsConstraintComponent::PostEditChangeProperty
	// 任何属性变化都更新 LocalOffset（简洁且可靠）
	UpdateLocalOffsetFromWorldPosition();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UEdenPinComponent::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);

	// 当组件被移动时，如果是 AttachToComponent 模式，更新 LocalOffset
	if (AttachmentMode == ERopePinAttachmentMode::AttachToComponent)
	{
		UpdateLocalOffsetFromWorldPosition();
	}
}
#endif