// Copyright Eden Games. All Rights Reserved.

#include "EdenRigidRopeAssetFactory.h"
#include "RigidRope/EdenRigidRopeAsset.h"

UEdenRigidRopeAssetFactory::UEdenRigidRopeAssetFactory()
{
	SupportedClass = UEdenRigidRopeAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UEdenRigidRopeAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent,
	FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UEdenRigidRopeAsset* NewAsset = NewObject<UEdenRigidRopeAsset>(InParent, InClass, InName, Flags);
	if (NewAsset)
	{
		NewAsset->RebuildPhysicsData();
	}
	return NewAsset;
}
