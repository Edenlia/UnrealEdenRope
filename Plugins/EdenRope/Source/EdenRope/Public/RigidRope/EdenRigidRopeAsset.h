// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Render/EdenRopeRenderTypes.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "EdenRigidRopeAsset.generated.h"

UENUM(BlueprintType)
enum class EEdenRopeForwardAxis : uint8
{
	PositiveX	UMETA(DisplayName="+X"),
	NegativeX	UMETA(DisplayName="-X"),
	PositiveY	UMETA(DisplayName="+Y"),
	NegativeY	UMETA(DisplayName="-Y"),
	PositiveZ	UMETA(DisplayName="+Z"),
	NegativeZ	UMETA(DisplayName="-Z"),
};

class UStaticMesh;
class UMaterialInterface;

// ============================================================================

USTRUCT(BlueprintType)
struct FEdenRopeSocket
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Socket")
	FName SocketName;

	/** Segment index this socket is attached to (0 ~ NumSegments-1) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Socket",
		meta=(ClampMin="0"))
	int32 ParentBodyIndex = 0;

	/** Transform relative to the parent body center */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Socket")
	FTransform RelativeTransform = FTransform::Identity;
};

// ============================================================================

/**
 * Persistent asset that fully describes a rigid-body rope:
 *   - Rope structure (segment count, length)
 *   - Per-segment physics bodies  (instanced UBodySetup array, mirrors UPhysicsAsset)
 *   - Per-joint constraint templates (instanced UPhysicsConstraintTemplate array)
 *   - Named sockets attached to bodies
 *   - Rendering parameters (tube / segment-mesh modes)
 *
 * Editing NumSegments / RopeLength / CollisionRadius triggers RebuildPhysicsData()
 * which regenerates all BodySetups and ConstraintTemplates.
 */
UCLASS(BlueprintType, hidecategories=Object)
class EDENROPE_API UEdenRigidRopeAsset : public UObject
{
	GENERATED_BODY()

public:
	UEdenRigidRopeAsset();

	// ==================== Rope Structure ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rope Structure",
		meta=(ClampMin="2", ClampMax="50", UIMin="2", UIMax="50"))
	int32 NumSegments = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope Structure",
		meta=(ClampMin="1.0", UIMin="1.0", UIMax="2000.0", Units="cm"))
	float RopeLength = 100.0f;

	// ==================== Physics: Bodies ====================
	// Mirrors UPhysicsAsset::SkeletalBodySetups.
	// N BodySetups, one per segment. BoneName = "Particle_0", "Particle_1", ...

	UPROPERTY(instanced)
	TArray<TObjectPtr<UBodySetup>> BodySetups;

	/** Mass applied to each segment's FBodyInstance at runtime (kg) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics|Body",
		meta=(ClampMin="0.01", UIMin="0.1", UIMax="100.0", Units="kg"))
	float SegmentMass = 1.0f;

	/** Capsule radius used when generating collision shapes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics|Body",
		meta=(ClampMin="0.1", UIMin="0.1", UIMax="50.0", Units="cm"))
	float CollisionRadius = 5.0f;

	// ==================== Physics: Constraints ====================
	// Mirrors UPhysicsAsset::ConstraintSetup.
	// N-1 ConstraintTemplates, one per adjacent joint.
	// ConstraintBone1(Parent) = "Particle_i", ConstraintBone2(Child) = "Particle_{i+1}"

	UPROPERTY(instanced)
	TArray<TObjectPtr<UPhysicsConstraintTemplate>> ConstraintSetup;

	/** Default swing limit angle (degrees) applied when rebuilding constraints */
	UPROPERTY(EditAnywhere, Category="Physics|Constraints|Defaults",
		meta=(ClampMin="1.0", ClampMax="180.0", UIMin="1.0", UIMax="90.0"))
	float DefaultSwingLimitAngle = 45.0f;

	/** Default swing constraint stiffness (soft constraint) */
	UPROPERTY(EditAnywhere, Category="Physics|Constraints|Defaults",
		meta=(ClampMin="0.0", UIMin="0.0", UIMax="1000.0"))
	float DefaultSwingStiffness = 50.0f;

	/** Default swing constraint damping */
	UPROPERTY(EditAnywhere, Category="Physics|Constraints|Defaults",
		meta=(ClampMin="0.0", UIMin="0.0", UIMax="100.0"))
	float DefaultSwingDamping = 0.0f;

	/** Default twist motion type */
	UPROPERTY(EditAnywhere, Category="Physics|Constraints|Defaults")
	TEnumAsByte<EAngularConstraintMotion> DefaultTwistMotion = EAngularConstraintMotion::ACM_Free;

	// ==================== Bone Naming ====================

	/**
	 * Prefix for auto-generated bone/socket names.
	 * When set (non-empty), generates unified names: {Prefix}_0, {Prefix}_1, ..., {Prefix}_N
	 *   - N+1 sockets (one per particle point)
	 *   - N bodies (one per segment), each mapped to the forward socket
	 *   - {Prefix}_i (i < N) has an associated FBodyInstance (Bodies[i])
	 *   - {Prefix}_N is socket-only (rope end, no body)
	 *
	 * The same names are used for UBodySetup::BoneName, socket queries, and
	 * GetBodyInstance() lookups, enabling SetConstrainedComponents / AttachToComponent
	 * to work directly with these bone names.
	 *
	 * When empty, falls back to default naming: "Particle_i" for both bodies and sockets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bone Naming")
	FString BoneNamePrefix;

	/** 当前第一个 Particle 的 Socket 名称（只读，由 BoneNamePrefix 自动生成） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bone Naming")
	FName FirstParticleSocketName;

	/** 根据当前 BoneNamePrefix 刷新 FirstParticleSocketName */
	void RefreshFirstParticleSocketName();

	// ==================== Sockets ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sockets")
	TArray<FEdenRopeSocket> Sockets;

	// ==================== Rendering ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering")
	ERopeRenderMode RenderMode = ERopeRenderMode::Tube;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
		meta=(ClampMin="0.01", UIMin="0.01", UIMax="50.0",
			EditCondition="RenderMode==ERopeRenderMode::Tube"))
	float CableWidth = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering",
		meta=(ClampMin="1", UIMax="16",
			EditCondition="RenderMode==ERopeRenderMode::Tube"))
	int32 NumSides = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
		meta=(UIMin="0.1", UIMax="8",
			EditCondition="RenderMode==ERopeRenderMode::Tube"))
	float TileMaterial = 1.0f;

	/** Chaikin smoothing iterations (0 = none, 1-3 = subdivide) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering",
		meta=(ClampMin="0", ClampMax="3", UIMin="0", UIMax="3",
			EditCondition="RenderMode==ERopeRenderMode::Tube"))
	uint8 Smoothing = 0;

	/** Decimation curvature threshold (0 = no decimation) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering",
		meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0",
			EditCondition="RenderMode==ERopeRenderMode::Tube"))
	float Decimation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
		meta=(EditCondition="RenderMode==ERopeRenderMode::SegmentMesh"))
	TObjectPtr<UStaticMesh> SegmentMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
		meta=(EditCondition="RenderMode==ERopeRenderMode::SegmentMesh"))
	ESegmentMeshForwardAxis SegmentMeshForwardAxis = ESegmentMeshForwardAxis::X;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
		meta=(EditCondition="RenderMode==ERopeRenderMode::SegmentMesh"))
	FVector2D SegmentMeshCrossScale = FVector2D(1.0f, 1.0f);

	/** Twist angle (degrees) on every other segment (0 = none, 90 = chain-link) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
		meta=(EditCondition="RenderMode==ERopeRenderMode::SegmentMesh"))
	float SegmentMeshTwistAngle = 0.0f;

	/** Forward-axis scale on top of auto-fit (1.0 = exact, >1.0 = overlap) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering",
		meta=(EditCondition="RenderMode==ERopeRenderMode::SegmentMesh",
			ClampMin="0.01"))
	float SegmentMeshForwardScale = 1.0f;

	// ==================== Editor Preview ====================

	/** Preview origin position (editor only, not used at runtime) */
	UPROPERTY(EditAnywhere, Category="Editor Preview")
	FVector PreviewOrigin = FVector(0.0f, 0.0f, 100.0f);

	/** Preview origin rotation (editor only, not used at runtime) */
	UPROPERTY(EditAnywhere, Category="Editor Preview")
	FRotator PreviewRotation = FRotator::ZeroRotator;

	/** Material used for editor preview rendering (editor only, not used at runtime) */
	UPROPERTY(EditAnywhere, Category="Editor Preview")
	TObjectPtr<UMaterialInterface> PreviewMaterial = nullptr;

	/** Forward axis used for the editor preview rope component */
	UPROPERTY(EditAnywhere, Category="Editor Preview")
	EEdenRopeForwardAxis PreviewForwardAxis = EEdenRopeForwardAxis::PositiveX;

	// ==================== Index Cache ====================

	/** BoneName -> array index, rebuilt by UpdateBodySetupIndexMap() */
	TMap<FName, int32> BodySetupIndexMap;

	// ==================== Public API ====================

	/** Computed segment length from current structure parameters */
	float GetSegmentLength() const;

	/** Rebuild all BodySetups and ConstraintSetup arrays from structure params */
	void RebuildPhysicsData();

	/** Rebuild the BodySetupIndexMap from current BodySetups */
	void UpdateBodySetupIndexMap();

	/** Find a body index by BoneName. Returns INDEX_NONE if not found. */
	int32 FindBodyIndex(FName BodyName) const;

	/** Find a constraint index by JointName. Returns INDEX_NONE if not found. */
	int32 FindConstraintIndex(FName ConstraintName) const;

	/** Find a socket by name. Returns nullptr if not found. */
	const FEdenRopeSocket* FindSocket(FName InSocketName) const;

	/** Static helper: build the default BoneName for a given index ("Particle_i" format) */
	static FName GetSegmentBoneName(int32 Index);

	/** Static helper: build the joint name for a given index */
	static FName GetJointName(int32 Index);

	/**
	 * Build bone/socket name for the given particle index using BoneNamePrefix.
	 * Falls back to GetSegmentBoneName() (returns "Particle_i") when BoneNamePrefix is empty.
	 */
	FName GetBoneName(int32 Index) const;

	// ==================== UObject Overrides ====================

	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** Create or reuse a BodySetup at the given array index */
	UBodySetup* EnsureBodySetup(int32 Index);

	/** Create or reuse a ConstraintTemplate at the given array index */
	UPhysicsConstraintTemplate* EnsureConstraintTemplate(int32 Index);

	/** Populate a single BodySetup with capsule geometry */
	void ConfigureBodySetup(UBodySetup* InBodySetup, int32 Index, float SegmentLength);

	/** Populate a single ConstraintTemplate with default joint params */
	void ConfigureConstraintTemplate(UPhysicsConstraintTemplate* InTemplate, int32 Index, float SegmentLength);
};
