// Copyright Eden Games. All Rights Reserved.

#include "EdenRopeSplineEdMode.h"
#include "HitProxies.h"
#include "ComponentVisualizer.h"
#include "UnrealWidget.h"
#include "Framework/Docking/TabManager.h"

const FEditorModeID FEdenRopeSplineEdMode::EM_EdenRopeSplineEdModeId = TEXT("EM_EdenRopeSplineEdMode");

static const FName EdenSplineEditorTabName("EdenSplineEditor");

bool FEdenRopeSplineEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	// Allow spline control-point clicks to pass through to the ComponentVisualizerManager.
	if (HitProxy && HitProxy->IsA(HComponentVisProxy::StaticGetType()))
	{
		return false;
	}

	// Allow gizmo-axis clicks to pass through so the transform widget works on spline points.
	if (HitProxy && HitProxy->IsA(HWidgetAxis::StaticGetType()))
	{
		return false;
	}

	// Any other click (backdrop, actor, BSP, etc.) is consumed.
	// Bring the Eden Spline Editor tab to the front as visual feedback.
	FGlobalTabmanager::Get()->TryInvokeTab(EdenSplineEditorTabName);

	return true;
}

bool FEdenRopeSplineEdMode::IsSelectionAllowed(AActor* InActor, bool bInSelection) const
{
	// Disallow all actor selection / deselection while spline editing is active.
	return false;
}
