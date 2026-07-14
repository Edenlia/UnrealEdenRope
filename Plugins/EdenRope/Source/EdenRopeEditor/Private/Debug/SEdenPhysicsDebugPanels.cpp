// Copyright Eden Games. All Rights Reserved.

#include "SEdenPhysicsDebugPanels.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SEdenPhysicsDebugPanels"

namespace EdenPhysicsDebugPanels
{
	// 共用的 "(Coming soon)" 居中占位控件
	static TSharedRef<SWidget> BuildPlaceholder(const FText& Label)
	{
		return
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
			.Padding(8.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
			];
	}
}

void SEdenPhysicsDebugBroadPhasePanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		EdenPhysicsDebugPanels::BuildPlaceholder(
			LOCTEXT("BroadPhaseComingSoon", "BroadPhase — (Coming soon)"))
	];
}

void SEdenPhysicsDebugNarrowPhasePanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		EdenPhysicsDebugPanels::BuildPlaceholder(
			LOCTEXT("NarrowPhaseComingSoon", "NarrowPhase — (Coming soon)"))
	];
}

#undef LOCTEXT_NAMESPACE

