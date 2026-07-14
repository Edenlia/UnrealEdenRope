// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class UEdenRigidRopeAsset;
class SEdenRigidRopeEditorViewport;
class IDetailsView;

class FEdenRigidRopeAssetEditor : public FAssetEditorToolkit
{
public:
	virtual ~FEdenRigidRopeAssetEditor() override;

	void InitEditor(const EToolkitMode::Type Mode,
		const TSharedPtr<IToolkitHost>& InitToolkitHost,
		UEdenRigidRopeAsset* InAsset);

	//~ FAssetEditorToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	//~ IToolkit interface
	virtual FName GetToolkitFName() const override { return TEXT("EdenRigidRopeAssetEditor"); }
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("EdenRigidRopeAsset "); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.8f, 0.5f, 0.2f, 0.5f); }

private:
	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);

	void ExtendToolbar();
	void OnToggleSimulate();
	bool IsSimulating() const;

	void OnAssetPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);

	TWeakObjectPtr<UEdenRigidRopeAsset> RopeAsset;
	TSharedPtr<SEdenRigidRopeEditorViewport> ViewportWidget;
	TSharedPtr<IDetailsView> DetailsView;

	bool bIsSimulating = false;
	FDelegateHandle PropertyChangedHandle;

	static const FName ViewportTabId;
	static const FName DetailsTabId;
};
