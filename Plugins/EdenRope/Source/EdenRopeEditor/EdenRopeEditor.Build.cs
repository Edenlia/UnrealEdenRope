// Copyright Eden Games. All Rights Reserved.

using UnrealBuildTool;

public class EdenRopeEditor : ModuleRules
{
	public EdenRopeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Access EdenRope Private headers (EdenRopeComponentBase.h etc.)
		PrivateIncludePaths.AddRange(
			new string[]
			{
				System.IO.Path.Combine(ModuleDirectory, "../EdenRope/Private"),
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"PropertyEditor",
				"ComponentVisualizers",
				"AssetTools",
				"AdvancedPreviewScene",
				"ToolMenus",
				"EdenRope",
				"Kismet",           // FBlueprintEditor, GetPreviewActor
				"SubobjectEditor",  // FSubobjectData (transitive dep of Kismet)
				"EditorFramework",
				"EditorSubsystem",        // UEdenPhysicsDebugSubsystem 基类
				"WorkspaceMenuStructure", // Tools 菜单挂载 Nomad Tab
			}
		);
	}
}
