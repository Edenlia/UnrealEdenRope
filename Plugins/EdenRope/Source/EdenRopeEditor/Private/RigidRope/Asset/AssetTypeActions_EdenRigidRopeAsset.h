// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

class FAssetTypeActions_EdenRigidRopeAsset : public FAssetTypeActions_Base
{
public:
	//~ FAssetTypeActions_Base interface
	virtual FText GetName() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual FColor GetTypeColor() const override;
	virtual uint32 GetCategories() override;
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;

	/** The custom category registered for "Eden Physics" */
	static EAssetTypeCategories::Type EdenPhysicsCategory;
};
