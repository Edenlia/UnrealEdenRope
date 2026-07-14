// Copyright Eden Games. All Rights Reserved.

#include "SEdenRigidRopeEditorViewport.h"
#include "EdenRigidRopeEditorViewportClient.h"
#include "RigidRope/EdenRigidRopeAsset.h"
#include "RigidRope/EdenRigidRopeActor.h"
#include "RigidRope/EdenRigidRopeComponent.h"
#include "AdvancedPreviewScene.h"

void SEdenRigidRopeEditorViewport::Construct(const FArguments& InArgs)
{
	RopeAsset = InArgs._RopeAsset;

	FAdvancedPreviewScene::ConstructionValues SceneArgs;
	SceneArgs.bDefaultLighting = true;
	PreviewScene = MakeShareable(new FAdvancedPreviewScene(SceneArgs));
	PreviewScene->SetFloorVisibility(true);

	SEditorViewport::Construct(SEditorViewport::FArguments());

	SpawnPreviewActor();
}

SEdenRigidRopeEditorViewport::~SEdenRigidRopeEditorViewport()
{
	DestroyPreviewActor();

	if (ViewportClient.IsValid())
	{
		ViewportClient->Viewport = nullptr;
		ViewportClient.Reset();
	}
}

void SEdenRigidRopeEditorViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(RopeAsset);
	Collector.AddReferencedObject(PreviewActor);
	Collector.AddReferencedObject(PreviewComponent);
}

TSharedRef<SEditorViewport> SEdenRigidRopeEditorViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SEdenRigidRopeEditorViewport::GetExtenders() const
{
	return MakeShareable(new FExtender);
}

void SEdenRigidRopeEditorViewport::OnFloatingButtonClicked()
{
}

TSharedRef<FEditorViewportClient> SEdenRigidRopeEditorViewport::MakeEditorViewportClient()
{
	ViewportClient = MakeShareable(new FEdenRigidRopeEditorViewportClient(
		*PreviewScene,
		SharedThis(this),
		RopeAsset));

	return ViewportClient.ToSharedRef();
}

#if UE_VERSION_NEWER_THAN(5, 7, 0)
TSharedPtr<SWidget> SEdenRigidRopeEditorViewport::BuildViewportToolbar()
{
	return SNew(SCommonEditorViewportToolbarBase, SharedThis(this));
}
#else
TSharedPtr<SWidget> SEdenRigidRopeEditorViewport::MakeViewportToolbar()
{
	return SNew(SCommonEditorViewportToolbarBase, SharedThis(this));
}
#endif

void SEdenRigidRopeEditorViewport::SpawnPreviewActor()
{
	if (!PreviewScene.IsValid() || !RopeAsset)
	{
		return;
	}

	UWorld* PreviewWorld = PreviewScene->GetWorld();
	if (!PreviewWorld)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FTransform SpawnTM(RopeAsset->PreviewRotation.Quaternion(), RopeAsset->PreviewOrigin);
	PreviewActor = PreviewWorld->SpawnActor<AEdenRigidRopeActor>(AEdenRigidRopeActor::StaticClass(), SpawnTM, SpawnParams);
	if (PreviewActor)
	{
		PreviewComponent = PreviewActor->RopeComponent;
		ApplyAssetToPreviewComponent();
		// Re-register so OnRegister re-creates Particles with correct NumSegments
		// and CreateSceneProxy picks up the correct rendering parameters
		PreviewComponent->UnregisterComponent();
		PreviewComponent->RegisterComponent();
	}
}

void SEdenRigidRopeEditorViewport::ApplyAssetToPreviewComponent()
{
	if (!PreviewComponent || !RopeAsset)
	{
		return;
	}

	PreviewComponent->RopeAsset = RopeAsset;
	PreviewComponent->ForwardAxis = RopeAsset->PreviewForwardAxis;
	PreviewComponent->BodyInstance.bSimulatePhysics = false;

	// 设置预览材质到 Material slot 0，供 GetRopeMaterial() / SceneProxy 使用
	if (RopeAsset->PreviewMaterial)
	{
		PreviewComponent->SetMaterial(0, RopeAsset->PreviewMaterial);
	}
}

void SEdenRigidRopeEditorViewport::DestroyPreviewActor()
{
	if (PreviewActor)
	{
		PreviewActor->Destroy();
		PreviewActor = nullptr;
		PreviewComponent = nullptr;
	}
}

void SEdenRigidRopeEditorViewport::ResetPreviewToRestPose()
{
	DestroyPreviewActor();
	SpawnPreviewActor();
}

void SEdenRigidRopeEditorViewport::RefreshPreview()
{
	if (!PreviewComponent || !RopeAsset)
	{
		return;
	}

	// 更新 PreviewActor 的 Transform，使其与 Asset 中的 PreviewOrigin/PreviewRotation 同步
	if (PreviewActor)
	{
		const FTransform NewTransform(RopeAsset->PreviewRotation.Quaternion(), RopeAsset->PreviewOrigin);
		PreviewActor->SetActorTransform(NewTransform);
	}

	ApplyAssetToPreviewComponent();
	PreviewComponent->UnregisterComponent();
	PreviewComponent->RegisterComponent();
}

void SEdenRigidRopeEditorViewport::SetSimulating(bool bInSimulating)
{
	bSimulating = bInSimulating;

	if (ViewportClient.IsValid())
	{
		ViewportClient->SetSimulating(bSimulating);
	}

	if (bSimulating)
	{
		if (!PreviewComponent)
		{
			SpawnPreviewActor();
		}
		if (PreviewComponent)
		{
			ApplyAssetToPreviewComponent();
			PreviewComponent->BodyInstance.bSimulatePhysics = true;
			PreviewComponent->UnregisterComponent();
			PreviewComponent->RegisterComponent();
		}
	}
	else
	{
		ResetPreviewToRestPose();
	}
}
