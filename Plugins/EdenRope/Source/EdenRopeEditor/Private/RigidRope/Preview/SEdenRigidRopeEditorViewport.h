// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/GCObject.h"

class FAdvancedPreviewScene;
class FEdenRigidRopeEditorViewportClient;
class UEdenRigidRopeAsset;
class AEdenRigidRopeActor;
class UEdenRigidRopeComponent;

class SEdenRigidRopeEditorViewport : public SEditorViewport, public FGCObject, public ICommonEditorViewportToolbarInfoProvider
{
public:
	SLATE_BEGIN_ARGS(SEdenRigidRopeEditorViewport) {}
		SLATE_ARGUMENT(UEdenRigidRopeAsset*, RopeAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SEdenRigidRopeEditorViewport() override;

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SEdenRigidRopeEditorViewport"); }

	//~ ICommonEditorViewportToolbarInfoProvider interface
	virtual TSharedRef<SEditorViewport> GetViewportWidget() override;
	virtual TSharedPtr<FExtender> GetExtenders() const override;
	virtual void OnFloatingButtonClicked() override;

	TSharedRef<FAdvancedPreviewScene> GetPreviewScene() { return PreviewScene.ToSharedRef(); }
	TSharedPtr<FEdenRigidRopeEditorViewportClient> GetRopeViewportClient() { return ViewportClient; }

	void SetSimulating(bool bInSimulating);
	bool IsSimulating() const { return bSimulating; }

	/** Re-apply asset properties to the preview component and rebuild its render state */
	void RefreshPreview();

protected:
	//~ SEditorViewport interface
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
#if UE_VERSION_NEWER_THAN(5, 7, 0)
	// UE5.8: SEditorViewport::MakeViewportToolbar() is now `final` (deprecated 5.7).
	// Use BuildViewportToolbar() to provide a toolbar that lives above the viewport.
	virtual TSharedPtr<SWidget> BuildViewportToolbar() override;
#else
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
#endif

private:
	void SpawnPreviewActor();
	void ApplyAssetToPreviewComponent();
	void DestroyPreviewActor();
	void ResetPreviewToRestPose();

	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<FEdenRigidRopeEditorViewportClient> ViewportClient;

	TObjectPtr<UEdenRigidRopeAsset> RopeAsset = nullptr;
	TObjectPtr<AEdenRigidRopeActor> PreviewActor = nullptr;
	TObjectPtr<UEdenRigidRopeComponent> PreviewComponent = nullptr;

	bool bSimulating = false;
};
