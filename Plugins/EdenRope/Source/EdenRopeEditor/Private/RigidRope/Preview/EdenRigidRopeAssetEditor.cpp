// Copyright Eden Games. All Rights Reserved.

#include "EdenRigidRopeAssetEditor.h"
#include "RigidRope/Preview/SEdenRigidRopeEditorViewport.h"
#include "EdenRigidRopeEditorViewportClient.h"
#include "RigidRope/EdenRigidRopeAsset.h"
#include "PropertyEditorModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "EdenRigidRopeAssetEditor"

const FName FEdenRigidRopeAssetEditor::ViewportTabId(TEXT("EdenRigidRopeAssetEditor_Viewport"));
const FName FEdenRigidRopeAssetEditor::DetailsTabId(TEXT("EdenRigidRopeAssetEditor_Details"));

FEdenRigidRopeAssetEditor::~FEdenRigidRopeAssetEditor()
{
	if (PropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
		PropertyChangedHandle.Reset();
	}
}

void FEdenRigidRopeAssetEditor::InitEditor(
	const EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InitToolkitHost,
	UEdenRigidRopeAsset* InAsset)
{
	RopeAsset = InAsset;

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout =
		FTabManager::NewLayout("Standalone_EdenRigidRopeAssetEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.7f)
				->AddTab(ViewportTabId, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.3f)
				->AddTab(DetailsTabId, ETabState::OpenedTab)
			)
		);

	InitAssetEditor(Mode, InitToolkitHost,
		TEXT("EdenRigidRopeAssetEditorApp"),
		StandaloneDefaultLayout,
		true, true,
		InAsset);

	ExtendToolbar();
	RegenerateMenusAndToolbars();

	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(
		this, &FEdenRigidRopeAssetEditor::OnAssetPropertyChanged);
}

void FEdenRigidRopeAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(
		LOCTEXT("WorkspaceMenu_EdenRigidRopeAssetEditor", "Rigid Rope Asset Editor"));

	InTabManager->RegisterTabSpawner(ViewportTabId,
		FOnSpawnTab::CreateSP(this, &FEdenRigidRopeAssetEditor::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(DetailsTabId,
		FOnSpawnTab::CreateSP(this, &FEdenRigidRopeAssetEditor::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FEdenRigidRopeAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(ViewportTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
}

FText FEdenRigidRopeAssetEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Rigid Rope Asset Editor");
}

TSharedRef<SDockTab> FEdenRigidRopeAssetEditor::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab).Label(LOCTEXT("ViewportTabLabel", "Viewport"));

	ViewportWidget = SNew(SEdenRigidRopeEditorViewport)
		.RopeAsset(RopeAsset.Get());

	SpawnedTab->SetContent(ViewportWidget.ToSharedRef());
	return SpawnedTab;
}

TSharedRef<SDockTab> FEdenRigidRopeAssetEditor::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.bShowPropertyMatrixButton = false;

	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	if (RopeAsset.IsValid())
	{
		DetailsView->SetObject(RopeAsset.Get());
	}

	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTabLabel", "Details"))
		[
			DetailsView.ToSharedRef()
		];
}

void FEdenRigidRopeAssetEditor::ExtendToolbar()
{
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.AddSeparator();
			ToolbarBuilder.AddToolBarButton(
				FUIAction(
					FExecuteAction::CreateSP(this, &FEdenRigidRopeAssetEditor::OnToggleSimulate),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &FEdenRigidRopeAssetEditor::IsSimulating)
				),
				NAME_None,
				LOCTEXT("Simulate", "Simulate"),
				LOCTEXT("SimulateTooltip", "Toggle physics simulation in the preview"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "PhysicsAssetEditor.Simulation.Start"),
				EUserInterfaceActionType::ToggleButton
			);
		})
	);

	AddToolbarExtender(ToolbarExtender);
}

void FEdenRigidRopeAssetEditor::OnToggleSimulate()
{
	bIsSimulating = !bIsSimulating;

	if (ViewportWidget.IsValid())
	{
		ViewportWidget->SetSimulating(bIsSimulating);
	}
}

bool FEdenRigidRopeAssetEditor::IsSimulating() const
{
	return bIsSimulating;
}

void FEdenRigidRopeAssetEditor::OnAssetPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (Object != RopeAsset.Get())
	{
		return;
	}

	if (ViewportWidget.IsValid() && !bIsSimulating)
	{
		ViewportWidget->RefreshPreview();
	}
}

#undef LOCTEXT_NAMESPACE
