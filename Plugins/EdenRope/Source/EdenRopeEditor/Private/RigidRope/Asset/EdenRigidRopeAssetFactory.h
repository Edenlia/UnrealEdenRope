// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "EdenRigidRopeAssetFactory.generated.h"

UCLASS()
class UEdenRigidRopeAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UEdenRigidRopeAssetFactory();

	//~ UFactory interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName,
		EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
};
