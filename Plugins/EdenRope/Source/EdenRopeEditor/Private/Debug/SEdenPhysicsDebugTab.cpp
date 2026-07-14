// Copyright Eden Games. All Rights Reserved.

#include "SEdenPhysicsDebugTab.h"
#include "SEdenPhysicsDebugTopBar.h"
#include "SEdenPhysicsDebugPanels.h"
#include "SEdenPhysicsDebugConstraintsPanel.h"
#include "EdenPhysicsDebugSubsystem.h"

#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SEdenPhysicsDebugTab"

void SEdenPhysicsDebugTab::Construct(const FArguments& InArgs)
{
	// 注册 Tab 条目（集中式注册，未来可扩展为数据表或注册函数）
	TabEntries.Add({
		FName(TEXT("BroadPhase")),
		LOCTEXT("BroadPhaseTab", "BroadPhase"),
		SNew(SEdenPhysicsDebugBroadPhasePanel)
	});
	TabEntries.Add({
		FName(TEXT("NarrowPhase")),
		LOCTEXT("NarrowPhaseTab", "NarrowPhase"),
		SNew(SEdenPhysicsDebugNarrowPhasePanel)
	});
	TabEntries.Add({
		FName(TEXT("Constraints")),
		LOCTEXT("ConstraintsTab", "Constraints"),
		SNew(SEdenPhysicsDebugConstraintsPanel)
	});

	ChildSlot
	[
		SNew(SVerticalBox)

		// 顶部工具行
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f)
		[
			SNew(SEdenPhysicsDebugTopBar)
		]

		// 分隔线
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 2.f)
		[
			SNew(SSeparator)
			.Orientation(Orient_Horizontal)
			.Thickness(1.f)
		]

		// 下方 Tab 区域
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f)
		[
			BuildTabSection()
		]
	];
}

SEdenPhysicsDebugTab::~SEdenPhysicsDebugTab()
{
}

TSharedRef<SWidget> SEdenPhysicsDebugTab::BuildTabSection()
{
	// 页签条：SSegmentedControl<int32>
	TSharedRef<SSegmentedControl<int32>> Segmented = SNew(SSegmentedControl<int32>)
		.Value(ActiveTabIndex)
		.OnValueChanged(this, &SEdenPhysicsDebugTab::OnTabValueChanged);

	for (int32 Index = 0; Index < TabEntries.Num(); ++Index)
	{
		Segmented->AddSlot(Index)
			.Text(TabEntries[Index].Label);
	}

	// 内容 Switcher
	TSharedRef<SWidgetSwitcher> Switcher = SNew(SWidgetSwitcher)
		.WidgetIndex(ActiveTabIndex);
	ContentSwitcher = Switcher;
	for (const FEdenDebugTabEntry& Entry : TabEntries)
	{
		Switcher->AddSlot()
		[
			Entry.Content
		];
	}

	return
		SNew(SVerticalBox)

		// Tab 页签条
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0.f, 2.f)
		[
			Segmented
		]

		// 分隔线
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 2.f)
		[
			SNew(SSeparator)
			.Orientation(Orient_Horizontal)
			.Thickness(1.f)
		]

		// 内容区
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(0.f, 2.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
			.Padding(2.f)
			[
				Switcher
			]
		];
}

void SEdenPhysicsDebugTab::OnTabValueChanged(int32 NewIndex)
{
	if (!TabEntries.IsValidIndex(NewIndex))
	{
		return;
	}
	ActiveTabIndex = NewIndex;
	if (ContentSwitcher.IsValid())
	{
		ContentSwitcher->SetActiveWidgetIndex(NewIndex);
	}
}

#undef LOCTEXT_NAMESPACE