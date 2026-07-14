// Copyright Eden Games. All Rights Reserved.

#include "EdenRopeEditorModule.h"
#include "PropertyEditorModule.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "RopeSpline/SEdenSplineEditorTab.h"
#include "Components/EdenRopeComponentDetailCustomization.h"
#include "RigidRope/EdenRigidRopeCompDetails.h"
#include "RopeSpline/EdenRopeComponentVisualizer.h"
#include "RopeSpline/EdenRopeSplineEdMode.h"
#include "RigidRope/Asset/AssetTypeActions_EdenRigidRopeAsset.h"
#include "Components/EdenRopeComponentBase.h"
#include "Components/EdenRopeComponent.h"
#include "Components/EdenRodComponent.h"
#include "RigidRope/EdenRigidRopeComponent.h"
#include "ComponentVisualizer.h"
#include "Editor/UnrealEdEngine.h"
#include "RigidRope/Asset/ActorFactoryEdenRigidRope.h"
#include "UnrealEdGlobals.h"
#include "EditorModeRegistry.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Subsystems/PlacementSubsystem.h"
#include "Debug/SEdenPhysicsDebugTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "EdenRopeEditor"

static const FName EdenRopeEditorModuleSplineEditorTabName("EdenSplineEditor");
static const FName EdenPhysicsDebugTabName("EdenPhysicsDebug");

void FEdenRopeEditorModule::StartupModule()
{
	// Register "Eden Physics" asset category and asset type actions
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FAssetTypeActions_EdenRigidRopeAsset::EdenPhysicsCategory =
		AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("EdenPhysics")), LOCTEXT("EdenPhysicsCategory", "Eden Physics"));

	{
		TSharedRef<IAssetTypeActions> RopeAssetActions = MakeShareable(new FAssetTypeActions_EdenRigidRopeAsset);
		AssetTools.RegisterAssetTypeActions(RopeAssetActions);
		RegisteredAssetTypeActions.Add(RopeAssetActions);
	}

	// Register detail customization for EdenRopeComponentBase (and derived)
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		"EdenRopeComponentBase",
		FOnGetDetailCustomizationInstance::CreateStatic(&FEdenRopeComponentDetailCustomization::MakeInstance)
	);
	// TODO: Comment Rigid Rope Detail Customization for now
	// PropertyModule.RegisterCustomClassLayout(
	// 	UEdenRigidRopeComponent::StaticClass()->GetFName(),
	// 	FOnGetDetailCustomizationInstance::CreateStatic(&FEdenRigidRopeCompDetails::MakeInstance)
	// );

	// Register component visualizer for EdenRopeComponentBase so the editing
	// spline is rendered in both Level Editor and Blueprint Editor viewports.
	RegisterComponentVisualizer(UEdenRopeComponentBase::StaticClass()->GetFName(), MakeShareable(new FEdenRopeComponentVisualizer));
	RegisterComponentVisualizer(UEdenRopeComponent::StaticClass()->GetFName(), MakeShareable(new FEdenRopeComponentVisualizer));
	RegisterComponentVisualizer(UEdenRodComponent::StaticClass()->GetFName(), MakeShareable(new FEdenRopeComponentVisualizer));
	
	// Register the spline-editing EdMode (locks selection while editing)
	FEditorModeRegistry::Get().RegisterMode<FEdenRopeSplineEdMode>(
		FEdenRopeSplineEdMode::EM_EdenRopeSplineEdModeId,
		LOCTEXT("EdenRopeSplineEdModeName", "Eden Rope Spline Edit"),
		FSlateIcon(),
		/*bVisible=*/false);

	// Register Nomad Tab for the spline editor window
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		EdenRopeEditorModuleSplineEditorTabName,
		FOnSpawnTab::CreateRaw(this, &FEdenRopeEditorModule::SpawnSplineEditorTab))
		.SetDisplayName(LOCTEXT("SplineEditorTabTitle", "Eden Spline Editor"))
		.SetTooltipText(LOCTEXT("SplineEditorTabTooltip", "Edit the authoring spline for EdenRope / EdenRod."))
		.SetAutoGenerateMenuEntry(false);

	// Register Nomad Tab for the Eden Physics Debug window (mounted under Tools menu)
	{
		const TSharedRef<FWorkspaceItem> ToolsCategory =
			WorkspaceMenu::GetMenuStructure().GetToolsCategory();

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			EdenPhysicsDebugTabName,
			FOnSpawnTab::CreateRaw(this, &FEdenRopeEditorModule::SpawnEdenPhysicsDebugTab))
			.SetDisplayName(LOCTEXT("EdenPhysicsDebugTabTitle", "Eden Physics Debug"))
			.SetTooltipText(LOCTEXT("EdenPhysicsDebugTabTooltip", "Open the Eden Physics debug window."))
			.SetGroup(ToolsCategory)
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Debug"));
	}

	// Register ActorFactory for drag-drop from Content Browser
	if (GEditor)
	{
		UActorFactoryEdenRigidRope* RopeActorFactory = NewObject<UActorFactoryEdenRigidRope>();
		GEditor->ActorFactories.Add(RopeActorFactory);

		// 同时注册到 PlacementSubsystem，拖拽放下时实际走的是这条路径
		if (UPlacementSubsystem* PlacementSubsystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
		{
			PlacementSubsystem->RegisterAssetFactory(TScriptInterface<IAssetFactoryInterface>(RopeActorFactory));
		}
	}
}

void FEdenRopeEditorModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(EdenRopeEditorModuleSplineEditorTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(EdenPhysicsDebugTabName);

	// Unregister asset type actions
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (const TSharedRef<IAssetTypeActions>& Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action);
		}
	}
	RegisteredAssetTypeActions.Empty();

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout("EdenRopeComponentBase");
		PropertyModule.UnregisterCustomClassLayout(UEdenRigidRopeComponent::StaticClass()->GetFName());
	}

	if (GUnrealEd)
	{
		for (FName ClassName : RegisteredComponentClassNames)
		{
			GUnrealEd->UnregisterComponentVisualizer(ClassName);
		}
	}
}

void FEdenRopeEditorModule::RegisterComponentVisualizer(FName ComponentClassName, TSharedPtr<FComponentVisualizer> Visualizer)
{
	if (GUnrealEd != nullptr)
	{
		GUnrealEd->RegisterComponentVisualizer(ComponentClassName, Visualizer);
	}

	RegisteredComponentClassNames.Add(ComponentClassName);

	if (Visualizer.IsValid())
	{
		Visualizer->OnRegister();
	}
}

TSharedRef<SDockTab> FEdenRopeEditorModule::SpawnSplineEditorTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedRef<SEdenSplineEditorTab> EditorWidget = SNew(SEdenSplineEditorTab);

	TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			EditorWidget
		];

	// Store a weak reference so the button callback can find the active tab widget
	SEdenSplineEditorTab::SetActiveInstance(EditorWidget);

	return DockTab;
}

TSharedRef<SDockTab> FEdenRopeEditorModule::SpawnEdenPhysicsDebugTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SEdenPhysicsDebugTab)
		];
}

IMPLEMENT_MODULE(FEdenRopeEditorModule, EdenRopeEditor)

#undef LOCTEXT_NAMESPACE
