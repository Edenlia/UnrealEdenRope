// Copyright Eden Games. All Rights Reserved.

#include "EdenRopeComponentVisualizer.h"
#include "Components/SplineComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"

void FEdenRopeComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	USplineComponent* SplineComp = FindEditingSplineComponent(Component);
	if (!SplineComp)
	{
		return;
	}

	// Delegate rendering (line, control points, hit-proxies) to the engine's
	// SplineComponentVisualizer which is already registered for USplineComponent.
	if (GUnrealEd)
	{
		TSharedPtr<FComponentVisualizer> SplineVis = GUnrealEd->FindComponentVisualizer(USplineComponent::StaticClass());
		if (SplineVis.IsValid())
		{
			SplineVis->DrawVisualization(SplineComp, View, PDI);
		}
	}
}

void FEdenRopeComponentVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	USplineComponent* SplineComp = FindEditingSplineComponent(Component);
	if (!SplineComp)
	{
		return;
	}

	if (GUnrealEd)
	{
		TSharedPtr<FComponentVisualizer> SplineVis = GUnrealEd->FindComponentVisualizer(USplineComponent::StaticClass());
		if (SplineVis.IsValid())
		{
			SplineVis->DrawVisualizationHUD(SplineComp, Viewport, View, Canvas);
		}
	}
}

USplineComponent* FEdenRopeComponentVisualizer::FindEditingSplineComponent(const UActorComponent* RopeComponent) const
{
	if (!RopeComponent)
	{
		return nullptr;
	}

	const AActor* Owner = RopeComponent->GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	// Search for a transient SplineComponent that is attached to the rope component.
	// The DetailCustomization creates exactly one such component when editing starts.
	TArray<USplineComponent*> SplineComps;
	Owner->GetComponents(SplineComps);

	for (USplineComponent* SplineComp : SplineComps)
	{
		if (SplineComp &&
			SplineComp->HasAnyFlags(RF_Transient) &&
			SplineComp->GetAttachParent() == RopeComponent)
		{
			return SplineComp;
		}
	}

	return nullptr;
}
