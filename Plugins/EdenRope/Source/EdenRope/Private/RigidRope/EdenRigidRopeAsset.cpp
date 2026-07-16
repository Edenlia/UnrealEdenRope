// Copyright Eden Games. All Rights Reserved.

#include "RigidRope/EdenRigidRopeAsset.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EdenRigidRopeAsset)

static const FString DefaultBonePrefix(TEXT("Particle_"));
static const FString JointPrefix(TEXT("Joint_"));

// ============================================================================

UEdenRigidRopeAsset::UEdenRigidRopeAsset()
{
}

// ============================================================================
// Static helpers
// ============================================================================

FName UEdenRigidRopeAsset::GetSegmentBoneName(int32 Index)
{
	return FName(*FString::Printf(TEXT("Particle_%d"), Index));
}

FName UEdenRigidRopeAsset::GetJointName(int32 Index)
{
	return FName(*FString::Printf(TEXT("Joint_%d"), Index));
}

FName UEdenRigidRopeAsset::GetBoneName(int32 Index) const
{
	if (BoneNamePrefix.IsEmpty())
	{
		return GetSegmentBoneName(Index);
	}
	return FName(*FString::Printf(TEXT("%s_%d"), *BoneNamePrefix, Index));
}

void UEdenRigidRopeAsset::RefreshFirstParticleSocketName()
{
	FirstParticleSocketName = GetBoneName(0);
}

// ============================================================================
// Queries
// ============================================================================

float UEdenRigidRopeAsset::GetSegmentLength() const
{
	return (NumSegments > 0) ? (RopeLength / NumSegments) : 0.0f;
}

int32 UEdenRigidRopeAsset::FindBodyIndex(FName BodyName) const
{
	const int32* Found = BodySetupIndexMap.Find(BodyName);
	return Found ? *Found : INDEX_NONE;
}

int32 UEdenRigidRopeAsset::FindConstraintIndex(FName ConstraintName) const
{
	for (int32 i = 0; i < ConstraintSetup.Num(); ++i)
	{
		if (ConstraintSetup[i] && ConstraintSetup[i]->DefaultInstance.JointName == ConstraintName)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

const FEdenRopeSocket* UEdenRigidRopeAsset::FindSocket(FName InSocketName) const
{
	for (const FEdenRopeSocket& Socket : Sockets)
	{
		if (Socket.SocketName == InSocketName)
		{
			return &Socket;
		}
	}
	return nullptr;
}

void UEdenRigidRopeAsset::UpdateBodySetupIndexMap()
{
	BodySetupIndexMap.Reset();
	for (int32 i = 0; i < BodySetups.Num(); ++i)
	{
		if (BodySetups[i])
		{
			BodySetupIndexMap.Add(BodySetups[i]->BoneName, i);
		}
	}
}

// ============================================================================
// Physics data rebuild
// ============================================================================

UBodySetup* UEdenRigidRopeAsset::EnsureBodySetup(int32 Index)
{
	if (BodySetups.IsValidIndex(Index) && BodySetups[Index])
	{
		return BodySetups[Index];
	}

	UBodySetup* NewSetup = NewObject<UBodySetup>(this, NAME_None, RF_Transactional);
	if (BodySetups.IsValidIndex(Index))
	{
		BodySetups[Index] = NewSetup;
	}
	else
	{
		BodySetups.Add(NewSetup);
	}
	return NewSetup;
}

UPhysicsConstraintTemplate* UEdenRigidRopeAsset::EnsureConstraintTemplate(int32 Index)
{
	if (ConstraintSetup.IsValidIndex(Index) && ConstraintSetup[Index])
	{
		return ConstraintSetup[Index];
	}

	UPhysicsConstraintTemplate* NewTemplate = NewObject<UPhysicsConstraintTemplate>(this, NAME_None, RF_Transactional);
	if (ConstraintSetup.IsValidIndex(Index))
	{
		ConstraintSetup[Index] = NewTemplate;
	}
	else
	{
		ConstraintSetup.Add(NewTemplate);
	}
	return NewTemplate;
}

void UEdenRigidRopeAsset::ConfigureBodySetup(UBodySetup* InBodySetup, int32 Index, float SegmentLength)
{
	check(InBodySetup);

	InBodySetup->BoneName = GetBoneName(Index);
	InBodySetup->bGenerateMirroredCollision = false;
	InBodySetup->bDoubleSidedGeometry = false;
	InBodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;

	InBodySetup->AggGeom.SphylElems.Reset();

	const float HalfLength = SegmentLength / 2.0f;

	FKSphylElem CapsuleElem;
	// Body 原点位于 Particle 端点，Capsule 几何中心偏移到 Segment 中点
	CapsuleElem.Center = FVector(HalfLength, 0.0f, 0.0f);
	// Capsule aligned along X axis (forward direction of segment)
	CapsuleElem.Rotation = FRotator(90.f, 0.f, 0.f);
	CapsuleElem.Radius = CollisionRadius;
	CapsuleElem.Length = FMath::Max(0.0f, SegmentLength - 2.0f * CollisionRadius);

	InBodySetup->AggGeom.SphylElems.Add(CapsuleElem);
	InBodySetup->CreatePhysicsMeshes();
}

void UEdenRigidRopeAsset::ConfigureConstraintTemplate(UPhysicsConstraintTemplate* InTemplate, int32 Index, float SegmentLength)
{
	check(InTemplate);

	FConstraintInstance& CI = InTemplate->DefaultInstance;
	CI.JointName = GetJointName(Index);
	CI.ConstraintBone1 = GetBoneName(Index);
	CI.ConstraintBone2 = GetBoneName(Index + 1);

	// Body 原点位于 Particle 端点：
	// Pos1 (ConstraintBone1 = Body[i])：从 Particle[i] 沿 +X 偏移整个 SegmentLength 到达交界处 Particle[i+1]
	// Pos2 (ConstraintBone2 = Body[i+1])：Body[i+1] 原点就在 Particle[i+1]，即交界处，无需偏移
	CI.Pos1 = FVector(SegmentLength, 0.0f, 0.0f);
	CI.Pos2 = FVector(0.0f, 0.0f, 0.0f);
	CI.PriAxis1 = FVector(1.0f, 0.0f, 0.0f);
	CI.SecAxis1 = FVector(0.0f, 1.0f, 0.0f);
	CI.PriAxis2 = FVector(1.0f, 0.0f, 0.0f);
	CI.SecAxis2 = FVector(0.0f, 1.0f, 0.0f);

	FConstraintProfileProperties& Profile = CI.ProfileInstance;

	// Linear: all locked (rigid distance between bodies)
	Profile.LinearLimit.XMotion = ELinearConstraintMotion::LCM_Locked;
	Profile.LinearLimit.YMotion = ELinearConstraintMotion::LCM_Locked;
	Profile.LinearLimit.ZMotion = ELinearConstraintMotion::LCM_Locked;

	// Angular: swing limited, twist configurable
	Profile.ConeLimit.Swing1Motion = EAngularConstraintMotion::ACM_Limited;
	Profile.ConeLimit.Swing1LimitDegrees = DefaultSwingLimitAngle;
	Profile.ConeLimit.Swing2Motion = EAngularConstraintMotion::ACM_Limited;
	Profile.ConeLimit.Swing2LimitDegrees = DefaultSwingLimitAngle;

	Profile.ConeLimit.bSoftConstraint = (DefaultSwingStiffness > 0.0f);
	Profile.ConeLimit.Stiffness = DefaultSwingStiffness;
	Profile.ConeLimit.Damping = DefaultSwingDamping;

	Profile.TwistLimit.TwistMotion = DefaultTwistMotion;

	// Disable collision between constrained bodies
	Profile.bDisableCollision = true;

#if WITH_EDITOR
	InTemplate->UpdateProfileInstance();
#endif
}

void UEdenRigidRopeAsset::RebuildPhysicsData()
{
	const int32 NumBodies = FMath::Max(NumSegments, 2);
	const int32 NumConstraints = NumBodies - 1;
	const float SegmentLength = RopeLength / NumBodies;

	// --- Bodies ---
	// Shrink if we have too many
	while (BodySetups.Num() > NumBodies)
	{
		BodySetups.RemoveAt(BodySetups.Num() - 1);
	}
	BodySetups.SetNum(NumBodies);

	for (int32 i = 0; i < NumBodies; ++i)
	{
		UBodySetup* BS = EnsureBodySetup(i);
		ConfigureBodySetup(BS, i, SegmentLength);
	}

	// --- Constraints ---
	while (ConstraintSetup.Num() > NumConstraints)
	{
		ConstraintSetup.RemoveAt(ConstraintSetup.Num() - 1);
	}
	ConstraintSetup.SetNum(NumConstraints);

	for (int32 i = 0; i < NumConstraints; ++i)
	{
		UPhysicsConstraintTemplate* CT = EnsureConstraintTemplate(i);
		ConfigureConstraintTemplate(CT, i, SegmentLength);
	}

	// --- Index map ---
	UpdateBodySetupIndexMap();

	// Clamp socket parent indices
	for (FEdenRopeSocket& Socket : Sockets)
	{
		Socket.ParentBodyIndex = FMath::Clamp(Socket.ParentBodyIndex, 0, NumBodies - 1);
	}

	// 刷新第一个 Particle 的 Socket 名称显示
	RefreshFirstParticleSocketName();

	MarkPackageDirty();
}

// ============================================================================
// UObject overrides
// ============================================================================

void UEdenRigidRopeAsset::PostLoad()
{
	Super::PostLoad();

	// Ensure index map is up to date after deserialization
	UpdateBodySetupIndexMap();

	// If loaded with no physics data (e.g. fresh asset), generate it
	if (BodySetups.Num() == 0)
	{
		RebuildPhysicsData();
	}
	else
	{
		const int32 NumBodies = FMath::Max(NumSegments, 2);
		const float SegmentLength = RopeLength / NumBodies;
		for (int32 i = 0; i < ConstraintSetup.Num(); ++i)
		{
			if (ConstraintSetup[i])
			{
				ConfigureConstraintTemplate(ConstraintSetup[i], i, SegmentLength);
			}
		}
	}

	// 确保加载后显示名称正确
	RefreshFirstParticleSocketName();
}

void UEdenRigidRopeAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
}

#if WITH_EDITOR

void UEdenRigidRopeAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	// Structure or collision params changed -> full rebuild
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeAsset, NumSegments)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeAsset, RopeLength)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeAsset, CollisionRadius)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeAsset, SegmentMass)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeAsset, DefaultSwingLimitAngle)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeAsset, DefaultSwingStiffness)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeAsset, DefaultSwingDamping)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeAsset, DefaultTwistMotion)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UEdenRigidRopeAsset, BoneNamePrefix))
	{
		RebuildPhysicsData();
	}
}

#endif
