// Copyright Eden Games. All Rights Reserved.

#include "EdenRopeComponentDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Components/SplineComponent.h"
#include "Components/EdenRopeComponentBase.h"
#include "RopeSpline/SEdenSplineEditorTab.h"

// For activating the component visualizer and EdMode
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "LevelEditorViewport.h"
#include "ComponentVisualizer.h"
#include "RopeSpline/EdenRopeSplineEdMode.h"
#include "EditorModeManager.h"

// For Blueprint editor Preview Actor lookup
#include "Engine/Blueprint.h"
#include "BlueprintEditor.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "GameFramework/Actor.h"
#include "Selection.h"

#define LOCTEXT_NAMESPACE "EdenRopeComponentDetailCustomization"

static const FName EdenRopeDetailCustomizationSplineEditorTabName("EdenSplineEditor");
// UE_DISABLE_OPTIMIZATION
TSharedRef<IDetailCustomization> FEdenRopeComponentDetailCustomization::MakeInstance()
{
	return MakeShareable(new FEdenRopeComponentDetailCustomization);
}

void FEdenRopeComponentDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	if (ObjectsBeingCustomized.Num() != 1)
	{
		return;
	}

	EditedComponent = Cast<UEdenRopeComponentBase>(ObjectsBeingCustomized[0].Get());
	if (!EditedComponent.IsValid())
	{
		return;
	}

	// Detect Blueprint editing: the component will be a template/archetype
	bIsBlueprintEditing = EditedComponent->IsTemplate();

	if (bIsBlueprintEditing)
	{
		// In Blueprint editor, find the live instance on the Preview Actor
		LiveRopeComp = FindLiveComponentOnPreviewActor();
	}
	else
	{
		// In Level editor, the edited component IS the live component
		LiveRopeComp = EditedComponent;
	}

	// Replace the placeholder bEditSplineButton property with a real toggle button
	TSharedRef<IPropertyHandle> EditSplinePropertyHandle = DetailBuilder.GetProperty(TEXT("bEditSplineButton"));
	if (EditSplinePropertyHandle->IsValidHandle())
	{
		DetailBuilder.EditDefaultProperty(EditSplinePropertyHandle)->CustomWidget()
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("Label_EditSpline", "Spline Editing"))
			.ToolTipText(LOCTEXT("Label_EditSpline_Tooltip", "Toggle spline editing for the initial curve shape."))
		]
		.ValueContent()
		.MinDesiredWidth(125.f)
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.ContentPadding(2)
			.Text(this, &FEdenRopeComponentDetailCustomization::GetEditButtonText)
			.OnClicked(this, &FEdenRopeComponentDetailCustomization::OnEditSplineClicked)
		];
	}
}

void FEdenRopeComponentDetailCustomization::PendingDelete()
{
	// Detail panel is being torn down — sync data back and clean up the transient spline
	EndEditSpline();
}

FText FEdenRopeComponentDetailCustomization::GetEditButtonText() const
{
	return bIsEditingSpline
		? LOCTEXT("Button_StopEditing", "Stop Editing")
		: LOCTEXT("Button_EditSpline", "Edit Spline");
}

FReply FEdenRopeComponentDetailCustomization::OnEditSplineClicked()
{
	if (!EditedComponent.IsValid())
	{
		return FReply::Unhandled();
	}

	if (bIsEditingSpline)
	{
		EndEditSpline();
		bIsEditingSpline = false;
	}
	else
	{
		bIsEditingSpline = StartEditSpline();
		// If Start failed, cleanup
		if (!bIsEditingSpline)
		{
			EndEditSpline();
		}
	}

	return FReply::Handled();
}

bool FEdenRopeComponentDetailCustomization::StartEditSpline()
{
	// In Blueprint mode, the Preview Actor may have been recreated since CustomizeDetails,
	// so re-resolve the live component each time editing starts.
	if (bIsBlueprintEditing)
	{
		LiveRopeComp = FindLiveComponentOnPreviewActor();
	}

	// ---- Start editing ----
	CreateSplineOnActor();

	if (!EditedComponent.IsValid())
	{
		return false;
	}

	// Open (or bring to front) the Nomad Tab
	FGlobalTabmanager::Get()->TryInvokeTab(EdenRopeDetailCustomizationSplineEditorTabName);
	TSharedPtr<SDockTab> SplineTab = FGlobalTabmanager::Get()->FindExistingLiveTab(EdenRopeDetailCustomizationSplineEditorTabName);
	if (!SplineTab.IsValid()) return false;
	SplineTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateSP(this, &FEdenRopeComponentDetailCustomization::OnSplineEditorTabClosed));

	// Pass the spline component to the tab widget
	TSharedPtr<SEdenSplineEditorTab> TabWidget = SEdenSplineEditorTab::GetActiveInstance();
	if (TabWidget.IsValid())
	{
		TabWidget->SetSplineComponent(EditingSplineComp);
	}
	else
	{
		return false;
	}

	// Activate the SplineComponentVisualizer so points are editable in viewport
	if (GUnrealEd)
	{
		TSharedPtr<FComponentVisualizer> Visualizer =
			GUnrealEd->FindComponentVisualizer(EditingSplineComp->GetClass());
		if (Visualizer.IsValid() && GCurrentLevelEditingViewportClient)
		{
			GUnrealEd->ComponentVisManager.SetActiveComponentVis(
				GCurrentLevelEditingViewportClient, Visualizer);
		}
	}
	else
	{
		return false;
	}

	// Force the Level Editor to rebuild VisualizersForSelection so the
	// newly-created transient SplineComponent is included in the draw list.
	if (GEditor && !bIsBlueprintEditing)
	{
		GEditor->NoteSelectionChange();
	}

	// Activate the selection-locking EdMode to prevent accidental
	// actor selection changes while spline editing is active.
	if (!bIsBlueprintEditing)
	{
		GLevelEditorModeTools().ActivateMode(FEdenRopeSplineEdMode::EM_EdenRopeSplineEdModeId);
	}
	else
	{
		// In Blueprint editor, ModeTools->HandleClick isn't available.
		// Bind to USelection::SelectionChangedEvent to detect and revert
		// accidental selection changes while spline editing.
		if (!SelectionChangedHandle.IsValid())
		{
			SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(
				this, &FEdenRopeComponentDetailCustomization::OnEditorSelectionChanged);
		}
	}

	return true;
}

void FEdenRopeComponentDetailCustomization::EndEditSpline()
{
	// Clear the flag first to prevent re-entrancy from OnTabClosed -> SyncAndDestroySpline.
	if (!bIsEditingSpline && !EditingSplineComp)
	{
		return;
	}

	if (EditingSplineComp)
	{
		// Sync data back to the TEMPLATE component's persistent AuthoringCurves.
		// In Blueprint mode, EditedComponent is the archetype — writing here ensures
		// the Blueprint asset is saved correctly.
		// In Level mode, EditedComponent is the live component itself.
		if (EditedComponent.IsValid())
		{
			EditedComponent->Modify();
			EditedComponent->AuthoringCurves = EditingSplineComp->SplineCurves;
			// 使用 FVector::OneVector 重建 ReparamTable，保证 Dist、SplineLen、ReparamTable 均为 Object Space
			EditedComponent->AuthoringCurves.UpdateSpline(false, false, 10, false, 0.0f, FVector::OneVector);
		}

		EditingSplineComp->DestroyComponent();
		EditingSplineComp = nullptr;	
	}

	// Deactivate the selection-locking EdMode (Level editor)
	if (GLevelEditorModeTools().IsModeActive(FEdenRopeSplineEdMode::EM_EdenRopeSplineEdModeId))
	{
		GLevelEditorModeTools().DeactivateMode(FEdenRopeSplineEdMode::EM_EdenRopeSplineEdModeId);
	}

	// Unbind the selection change guard (Blueprint editor)
	if (SelectionChangedHandle.IsValid())
	{
		USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);
		SelectionChangedHandle.Reset();
	}

	// Close the Eden Spline Editor tab (if open).
	// bIsEditingSpline is already false, so the OnTabClosed callback won't recurse.
	TSharedPtr<SDockTab> SplineTab = FGlobalTabmanager::Get()->FindExistingLiveTab(EdenRopeDetailCustomizationSplineEditorTabName);
	if (SplineTab.IsValid())
	{
		// Clear Callback
		SplineTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
		SplineTab->RequestCloseTab();
	}
}

void FEdenRopeComponentDetailCustomization::CreateSplineOnActor()
{
	if (EditingSplineComp)
	{
		return; // Already created
	}

	if (!EditedComponent.IsValid())
	{
		return;
	}

	// Determine which component/actor to create the SplineComp on
	UEdenRopeComponentBase* TargetComp = LiveRopeComp.Get();
	if (!TargetComp)
	{
		return;
	}

	AActor* Owner = TargetComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	EditingSplineComp = NewObject<USplineComponent>(
		Owner,
		USplineComponent::StaticClass(),
		MakeUniqueObjectName(Owner, USplineComponent::StaticClass(), TEXT("EdenRopeAuthoringSpline")),
		RF_Transient | RF_DuplicateTransient | RF_Transactional);

	// SplineComponent 附着到 RopeComponent，RelativeTransform 为 Identity。
	// 因此 SplineCurves.Position 中的控制点数据始终位于 RopeComponent 的局部空间中。
	// UE 的 SplineComponentVisualizer 编辑时会将世界空间坐标逆变换回 SplineComponent 局部空间再存入 SplineCurves，
	// 该逆变换包含了 Scale 的去除，所以即使组件有非默认 Scale，曲线数据仍然是纯局部空间坐标。
	EditingSplineComp->SetupAttachment(TargetComp);
	EditingSplineComp->SetRelativeScale3D(FVector(1.f)); // 维持与Target Component同scale
	EditingSplineComp->SetMobility(EComponentMobility::Movable);
	EditingSplineComp->bDrawDebug = true;
	EditingSplineComp->SetVisibility(true);
	EditingSplineComp->SetClosedLoop(false);
	EditingSplineComp->SetHiddenInGame(true);
	EditingSplineComp->DefaultUpVector = FVector::UpVector;

	// Push persistent AuthoringCurves from the template/edited component into the transient spline
	EditingSplineComp->SplineCurves = EditedComponent->AuthoringCurves;
	EditingSplineComp->UpdateSpline();

	EditingSplineComp->RegisterComponent();
}

void FEdenRopeComponentDetailCustomization::OnSplineEditorTabClosed(TSharedRef<SDockTab> ClosedTab)
{
	EndEditSpline();
	bIsEditingSpline = false;
}

void FEdenRopeComponentDetailCustomization::OnEditorSelectionChanged(UObject* NewSelection)
{
	// Only guard while spline editing is active in Blueprint mode
	if (!bIsEditingSpline || !bIsBlueprintEditing || bRestoringSelection)
	{
		return;
	}

	// If the selection changed away from our component, bring the spline editor tab
	// to the front as visual feedback that the click was "intercepted".
	FGlobalTabmanager::Get()->TryInvokeTab(EdenRopeDetailCustomizationSplineEditorTabName);
}

UEdenRopeComponentBase* FEdenRopeComponentDetailCustomization::FindLiveComponentOnPreviewActor() const
{
	if (!EditedComponent.IsValid() || !EditedComponent->IsTemplate())
	{
		return nullptr;
	}

	// --- Pattern from SplineComponentDetails::GetSplineComponentToVisualize() ---

	// Determine the Blueprint-generated class
	const UClass* BPClass = nullptr;
	if (const AActor* OwningCDO = EditedComponent->GetOwner())
	{
		// Native component template: Owner is the CDO
		BPClass = OwningCDO->GetClass();
	}
	else
	{
		// Non-native component template (added in Blueprint editor): Outer is the BP-generated class
		BPClass = Cast<UClass>(EditedComponent->GetOuter());
	}

	if (!BPClass)
	{
		return nullptr;
	}

	UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(BPClass);
	if (!Blueprint)
	{
		return nullptr;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return nullptr;
	}

	FBlueprintEditor* BlueprintEditor = StaticCast<FBlueprintEditor*>(
		AssetEditorSubsystem->FindEditorForAsset(Blueprint, /*bFocusIfOpen=*/false));
	if (!BlueprintEditor)
	{
		return nullptr;
	}

	AActor* PreviewActor = BlueprintEditor->GetPreviewActor();
	if (!PreviewActor)
	{
		return nullptr;
	}

	// Find the instance of our component on the Preview Actor via GetArchetypeInstances
	TArray<UObject*> Instances;
	EditedComponent->GetArchetypeInstances(Instances);

	for (UObject* Instance : Instances)
	{
		UEdenRopeComponentBase* CompInstance = Cast<UEdenRopeComponentBase>(Instance);
		if (CompInstance && CompInstance->GetOwner() == PreviewActor)
		{
			return CompInstance;
		}
	}

	return nullptr;
}
// UE_ENABLE_OPTIMIZATION
#undef LOCTEXT_NAMESPACE
