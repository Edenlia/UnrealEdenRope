// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class FAdvancedPreviewScene;
class UEdenRigidRopeAsset;

class FEdenRigidRopeEditorViewportClient : public FEditorViewportClient
{
public:
	FEdenRigidRopeEditorViewportClient(FAdvancedPreviewScene& InPreviewScene,
		const TSharedRef<class SEdenRigidRopeEditorViewport>& InViewport,
		UEdenRigidRopeAsset* InAsset);

	//~ FEditorViewportClient interface
	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;

	//~ FGCObject interface (inherited from FEditorViewportClient)
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FEdenRigidRopeEditorViewportClient"); }

	void SetAsset(UEdenRigidRopeAsset* InAsset) { RopeAsset = InAsset; }

	void SetSimulating(bool bInSimulating) { bSimulating = bInSimulating; }
	bool IsSimulating() const { return bSimulating; }

private:
	void DrawConstraints(FPrimitiveDrawInterface* PDI);

	/** Compute body transform for a segment at rest (straight line along X) */
	FTransform GetRestBodyTransform(int32 BodyIndex) const;

	TWeakObjectPtr<UEdenRigidRopeAsset> RopeAsset;
	bool bSimulating = false;
};
