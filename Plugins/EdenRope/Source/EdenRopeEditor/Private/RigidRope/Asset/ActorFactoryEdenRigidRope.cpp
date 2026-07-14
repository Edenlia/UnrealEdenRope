// Copyright Eden Games. All Rights Reserved.

#include "ActorFactoryEdenRigidRope.h"
#include "RigidRope/EdenRigidRopeAsset.h"
#include "RigidRope/EdenRigidRopeActor.h"
#include "RigidRope/EdenRigidRopeComponent.h"

UActorFactoryEdenRigidRope::UActorFactoryEdenRigidRope()
{
	DisplayName = FText::FromString(TEXT("Eden Rigid Rope"));
	NewActorClass = AEdenRigidRopeActor::StaticClass();
	bUseSurfaceOrientation = false;
}

bool UActorFactoryEdenRigidRope::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (!AssetData.IsValid() || !AssetData.IsInstanceOf(UEdenRigidRopeAsset::StaticClass()))
	{
		OutErrorMsg = FText::FromString(TEXT("A valid Eden Rigid Rope Asset must be specified."));
		return false;
	}

	return true;
}

void UActorFactoryEdenRigidRope::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	UEdenRigidRopeAsset* RopeAsset = Cast<UEdenRigidRopeAsset>(Asset);
	AEdenRigidRopeActor* RopeActor = Cast<AEdenRigidRopeActor>(NewActor);

	if (RopeAsset && RopeActor && RopeActor->RopeComponent)
	{
		RopeActor->RopeComponent->SetRopeAsset(RopeAsset);
	}
}

UObject* UActorFactoryEdenRigidRope::GetAssetFromActorInstance(AActor* ActorInstance)
{
	AEdenRigidRopeActor* RopeActor = Cast<AEdenRigidRopeActor>(ActorInstance);
	if (RopeActor && RopeActor->RopeComponent)
	{
		return RopeActor->RopeComponent->RopeAsset;
	}
	return nullptr;
}
