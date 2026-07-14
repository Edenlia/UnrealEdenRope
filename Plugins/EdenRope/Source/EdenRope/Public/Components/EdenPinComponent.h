// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Handles/EdenPinConstraintHandle.h"
#include "EdenPinComponent.generated.h"

struct FEdenRopeHandle;
class UEdenRopeComponentBase;
class UEdenRopeComponent;
class UPrimitiveComponent;

/**
 * Pin 约束连接模式
 */
UENUM(BlueprintType)
enum class ERopePinAttachmentMode : uint8
{
	/** 固定到世界空间位置（使用组件自身位置） */
	WorldPosition,
	
	/** 连接到 PrimitiveComponent（支持动态刚体） */
	AttachToComponent
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class EDENROPE_API UEdenPinComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UEdenPinComponent();

protected:
	virtual void BeginPlay() override;

	void RegisterPinConstraint(const FEdenRopeHandle& RopeHandle);

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditComponentMove(bool bFinished) override;
#endif

	/** 获取被 Pin 的 RopeComponent（仅 Rope；Rod 返回 nullptr） */
	UEdenRopeComponent* GetPinnedRopeComponent() const;

	/** 获取被 Pin 的 RopeComponentBase（Rope 和 Rod 都可返回） */
	UEdenRopeComponentBase* GetPinnedRopeComponentBase() const;

	/** 获取附着的 PrimitiveComponent */
	UPrimitiveComponent* GetAttachedPrimitiveComponent() const;

	/**
	 * 根据当前组件位置更新 LocalOffset
	 * 当 AttachmentMode = AttachToComponent 时，将当前世界位置转换为相对于 Attached 组件的局部偏移
	 */
	UFUNCTION(BlueprintCallable, Category = "Pin")
	void UpdateLocalOffsetFromWorldPosition();

	// ========== Pin 到绳索的设置 ==========
	
	/**
	 * 被 Pin 的绳索所在的 Actor
	 * 如果为 NULL，将在 Owner Actor 中查找
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Pin|Rope")
	TObjectPtr<AActor> PinnedRopeActor;

	/**
	 * 被 Pin 的 EdenRopeComponent 名称
	 * 如果为空，将使用找到的第一个 EdenRopeComponent
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin|Rope")
	FName PinnedRopeComponentName;

	/** 被 Pin 的粒子索引 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin|Rope")
	int32 PinnedParticleIndex = 0;

	// ========== 旋转约束（Rod 专用）==========

	/**
	 * 是否同时约束段方向（仅对 Rod 有效；Rope 自动忽略）
	 * 开启后，端点段方向被锁定到此组件的世界旋转
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin|Orientation")
	bool bConstrainOrientation = true;

	/** 运行时缓存：当前 Pin 的 owner 组件是否含段方向数据（Rod） */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Pin|Orientation")
	bool bOwnerIsRod = false;

	// ========== Pin 连接模式设置 ==========
	
	/** Pin 约束连接模式 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin|Attachment")
	ERopePinAttachmentMode AttachmentMode = ERopePinAttachmentMode::WorldPosition;

	/**
	 * 要连接的 PrimitiveComponent 所在的 Actor
	 * 如果为 NULL，将在 Owner Actor 中查找
	 * 仅当 AttachmentMode = AttachToComponent 时有效
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Pin|Attachment", 
		meta = (EditCondition = "AttachmentMode == ERopePinAttachmentMode::AttachToComponent"))
	TObjectPtr<AActor> AttachedActor;

	/**
	 * 要连接的 PrimitiveComponent 名称
	 * 如果为空，将使用找到的第一个带物理的 PrimitiveComponent
	 * 仅当 AttachmentMode = AttachToComponent 时有效
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin|Attachment",
		meta = (EditCondition = "AttachmentMode == ERopePinAttachmentMode::AttachToComponent"))
	FName AttachedComponentName;

	/**
	 * 相对于附着组件的局部空间偏移（单位：cm）
	 * 仅当 AttachmentMode = AttachToComponent 时有效
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin|Attachment",
		meta = (EditCondition = "AttachmentMode == ERopePinAttachmentMode::AttachToComponent"))
	FVector LocalOffset = FVector::ZeroVector;

	/**
	 * 运行时直接设置附着组件（类似 PhysicsConstraintComponent::OverrideComponent）
	 * 不序列化，供代码使用
	 */
	TWeakObjectPtr<UPrimitiveComponent> OverrideAttachedComponent;

	/**
	 * 运行时设置附着组件
	 * @param InComponent 要附着的组件
	 * @param InLocalOffset 局部偏移
	 */
	UFUNCTION(BlueprintCallable, Category = "Pin")
	void SetAttachedComponent(UPrimitiveComponent* InComponent, FVector InLocalOffset = FVector::ZeroVector);

	FEdenPinConstraintHandle PinConstraintHandle;

private:
	/** 实际注册类型跟踪：true = 注册为 WorldPosition（需要 Tick 更新位置） */
	bool bRegisteredAsWorldPosition = false;

	/** 是否同时注册了旋转约束（Rod + bConstrainOrientation + WorldPosition） */
	bool bHasOrientationConstraint = false;

#if WITH_EDITOR
	/** 防止 UpdateLocalOffsetFromWorldPosition 递归调用的标志 */
	bool bIsUpdatingLocalOffset = false;
#endif
};
