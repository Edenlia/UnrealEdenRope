// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class FComponentVisualizer;
class IAssetTypeActions;

class FEdenRopeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Register a component visualizer (mirrors FWaterEditorModule pattern). */
	void RegisterComponentVisualizer(FName ComponentClassName, TSharedPtr<FComponentVisualizer> Visualizer);

	TSharedRef<class SDockTab> SpawnSplineEditorTab(const FSpawnTabArgs& SpawnTabArgs);

	/** 生成 Eden Physics Debug Nomad Tab 的内容 */
	TSharedRef<class SDockTab> SpawnEdenPhysicsDebugTab(const FSpawnTabArgs& SpawnTabArgs);

	/** Class names we registered visualizers for, so we can unregister them on shutdown. */
	TArray<FName> RegisteredComponentClassNames;

	/** Asset type actions registered with IAssetTools, unregistered on shutdown. */
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
};
