// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

class USplineComponent;

/**
 * Component visualizer for UEdenRopeComponentBase.
 *
 * When spline editing is active, a transient USplineComponent is attached to
 * the rope component.  This visualizer finds that transient spline and
 * delegates drawing to the engine's built-in FSplineComponentVisualizer so that
 * the spline line, control points, and hit-proxies are rendered in the viewport.
 *
 * Works in both Level Editor and Blueprint Editor viewports.
 */
class FEdenRopeComponentVisualizer : public FComponentVisualizer
{
public:
	//~ FComponentVisualizer interface
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;

private:
	/** Find the transient editing SplineComponent attached to the given component. */
	USplineComponent* FindEditingSplineComponent(const UActorComponent* RopeComponent) const;
};
