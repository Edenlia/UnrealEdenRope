// Copyright Eden Games. All Rights Reserved.

#include "AssetTypeActions_EdenRigidRopeAsset.h"
#include "RigidRope/Preview/EdenRigidRopeAssetEditor.h"
#include "RigidRope/EdenRigidRopeAsset.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_EdenRigidRopeAsset"

EAssetTypeCategories::Type FAssetTypeActions_EdenRigidRopeAsset::EdenPhysicsCategory = EAssetTypeCategories::None;

FText FAssetTypeActions_EdenRigidRopeAsset::GetName() const
{
	return LOCTEXT("Name", "Eden Rigid Rope");
}

UClass* FAssetTypeActions_EdenRigidRopeAsset::GetSupportedClass() const
{
	return UEdenRigidRopeAsset::StaticClass();
}

FColor FAssetTypeActions_EdenRigidRopeAsset::GetTypeColor() const
{
	return FColor(200, 120, 60);
}

uint32 FAssetTypeActions_EdenRigidRopeAsset::GetCategories()
{
	return EdenPhysicsCategory;
}

void FAssetTypeActions_EdenRigidRopeAsset::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid()
		? EToolkitMode::WorldCentric
		: EToolkitMode::Standalone;

	for (UObject* Obj : InObjects)
	{
		UEdenRigidRopeAsset* Asset = Cast<UEdenRigidRopeAsset>(Obj);
		if (Asset)
		{
			TSharedRef<FEdenRigidRopeAssetEditor> Editor = MakeShared<FEdenRigidRopeAssetEditor>();
			Editor->InitEditor(Mode, EditWithinLevelEditor, Asset);
		}
	}
}

#undef LOCTEXT_NAMESPACE
