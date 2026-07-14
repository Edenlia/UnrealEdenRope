// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class IDetailsView;
class USplineComponent;

/**
 * Nomad-tab widget that hosts a SDetailsView for the authoring USplineComponent.
 * The engine's built-in FSplineComponentDetails customization automatically
 * provides the "Selected Points" panel inside this view.
 */
class SEdenSplineEditorTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEdenSplineEditorTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Set the spline component whose details should be displayed. */
	void SetSplineComponent(USplineComponent* InSpline);

	// ------ Static accessor so the button callback can reach the live tab ------
	static void SetActiveInstance(TSharedPtr<SEdenSplineEditorTab> InInstance);
	static TSharedPtr<SEdenSplineEditorTab> GetActiveInstance();

private:
	TSharedPtr<IDetailsView> SplineDetailsView;

	static TWeakPtr<SEdenSplineEditorTab> ActiveInstance;
};
