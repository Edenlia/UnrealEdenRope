// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class UEdenRopeComponentBase;
class USplineComponent;
class SDockTab;

/**
 * Detail customization for UEdenRopeComponentBase (and derived classes).
 * Manages a transient USplineComponent for viewport spline editing:
 *   - Created on-demand when "Edit Spline" button is clicked
 *   - Destroyed (with data synced back to AuthoringCurves) on "Stop Editing" or PendingDelete
 *
 * Supports both Level editing and Blueprint editing:
 *   - Level: SplineComp is created on the selected Actor directly
 *   - Blueprint: SplineComp is created on the Preview Actor (found via GetArchetypeInstances)
 */
class FEdenRopeComponentDetailCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	virtual void PendingDelete() override;

private:
	/** Toggle handler: starts or stops spline editing */
	FReply OnEditSplineClicked();

	bool StartEditSpline();
	void EndEditSpline();

	/** Returns the button label based on editing state */
	FText GetEditButtonText() const;

	/** Create a transient USplineComponent on the target Actor, push AuthoringCurves into it */
	void CreateSplineOnActor();
	
	/**
	 * For Blueprint editing: find the live instance of EditedComponent on the Preview Actor.
	 * Uses the engine pattern from SplineComponentDetails / ParticleSystemComponentDetails.
	 * Returns nullptr if not in Blueprint editing or if the preview instance cannot be found.
	 */
	UEdenRopeComponentBase* FindLiveComponentOnPreviewActor() const;

	/** The component being customized (may be a template/archetype in Blueprint editor) */
	TWeakObjectPtr<UEdenRopeComponentBase> EditedComponent;

	/** The live component to attach the SplineComp to.
	 *  - Level editing: same as EditedComponent
	 *  - Blueprint editing: the instance on the Preview Actor */
	TWeakObjectPtr<UEdenRopeComponentBase> LiveRopeComp;

	/** Called when USelection::SelectionChangedEvent fires (Blueprint editing guard) */
	void OnEditorSelectionChanged(UObject* NewSelection);

	/** Called when the Eden Spline Editor tab is closed by the user */
	void OnSplineEditorTabClosed(TSharedRef<SDockTab> ClosedTab);

	TObjectPtr<USplineComponent> EditingSplineComp = nullptr;
	bool bIsEditingSpline = false;
	bool bIsBlueprintEditing = false;

	/** Delegate handle for USelection::SelectionChangedEvent, used to guard selection in BP mode */
	FDelegateHandle SelectionChangedHandle;

	/** Guard flag to avoid re-entrancy when restoring selection */
	bool bRestoringSelection = false;
};
