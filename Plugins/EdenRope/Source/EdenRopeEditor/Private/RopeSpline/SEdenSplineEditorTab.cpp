// Copyright Eden Games. All Rights Reserved.

#include "SEdenSplineEditorTab.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "Components/SplineComponent.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "EdenSplineEditorTab"

TWeakPtr<SEdenSplineEditorTab> SEdenSplineEditorTab::ActiveInstance;

void SEdenSplineEditorTab::Construct(const FArguments& InArgs)
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs ViewArgs;
	ViewArgs.bAllowSearch = false;
	ViewArgs.bHideSelectionTip = true;
	ViewArgs.bShowObjectLabel = true;
	ViewArgs.NameAreaSettings = FDetailsViewArgs::ObjectsUseNameArea;

	SplineDetailsView = PropertyModule.CreateDetailView(ViewArgs);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Header", "Eden Spline Editor"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SplineDetailsView.ToSharedRef()
		]
	];
}

void SEdenSplineEditorTab::SetSplineComponent(USplineComponent* InSpline)
{
	if (SplineDetailsView.IsValid())
	{
		if (InSpline)
		{
			SplineDetailsView->SetObject(InSpline);
		}
		else
		{
			SplineDetailsView->SetObject(nullptr);
		}
	}
}

void SEdenSplineEditorTab::SetActiveInstance(TSharedPtr<SEdenSplineEditorTab> InInstance)
{
	ActiveInstance = InInstance;
}

TSharedPtr<SEdenSplineEditorTab> SEdenSplineEditorTab::GetActiveInstance()
{
	return ActiveInstance.Pin();
}

#undef LOCTEXT_NAMESPACE
