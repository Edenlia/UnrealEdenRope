// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "ActorFactories/ActorFactory.h"
#include "ActorFactoryEdenRigidRope.generated.h"

/**
 * Actor factory that creates AEdenRigidRopeActor when a UEdenRigidRopeAsset
 * is dragged from the Content Browser into the Level viewport.
 */
UCLASS()
class UActorFactoryEdenRigidRope : public UActorFactory
{
	GENERATED_BODY()

public:
	UActorFactoryEdenRigidRope();

	//~ Begin UActorFactory Interface
	virtual bool CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg) override;
	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
	virtual UObject* GetAssetFromActorInstance(AActor* ActorInstance) override;
	//~ End UActorFactory Interface
};
