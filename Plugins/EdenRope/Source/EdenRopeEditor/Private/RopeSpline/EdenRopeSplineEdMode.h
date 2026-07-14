// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

/**
 * Lightweight editor mode activated while spline editing is in progress.
 *
 * Responsibilities:
 *   - Intercept viewport clicks that are NOT on spline control points (HComponentVisProxy)
 *     or gizmo axes (HWidgetAxis), consuming them so the user cannot accidentally
 *     select a different Actor or deselect the current one.
 *   - Bring the Eden Spline Editor tab to the front whenever such a "blocked" click occurs.
 *   - Block all actor selection changes via IsSelectionAllowed().
 */
class FEdenRopeSplineEdMode : public FEdMode
{
public:
	static const FEditorModeID EM_EdenRopeSplineEdModeId;

	//~ FEdMode interface
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual bool IsSelectionAllowed(AActor* InActor, bool bInSelection) const override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override { return true; }
	virtual bool UsesTransformWidget() const override { return true; }
};
