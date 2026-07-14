// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UEdenPhysicsDebugSubsystem;
class SWidgetSwitcher;

/**
 * Eden Physics Debug 窗口根 Widget
 *
 * 结构：
 *   SVerticalBox
 *   ├── [AutoHeight] 顶部工具行 SEdenPhysicsDebugTopBar
 *   ├── [AutoHeight] Tab 页签条（SSegmentedControl：BroadPhase / NarrowPhase / Constraints ...）
 *   └── [FillHeight(1)] SWidgetSwitcher 内容区
 */
class SEdenPhysicsDebugTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEdenPhysicsDebugTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SEdenPhysicsDebugTab();

private:
	/** Tab 条目注册，便于未来集中式扩展新 Tab */
	struct FEdenDebugTabEntry
	{
		FName Id;
		FText Label;
		TSharedRef<SWidget> Content;
	};

	/** 构建下方 Tab 容器（SSegmentedControl + SWidgetSwitcher） */
	TSharedRef<SWidget> BuildTabSection();

	/** SSegmentedControl 值变更回调 */
	void OnTabValueChanged(int32 NewIndex);

private:
	/** 已注册的 Tab 条目（按顺序） */
	TArray<FEdenDebugTabEntry> TabEntries;

	/** 切换下方 Tab 内容的 Switcher */
	TSharedPtr<SWidgetSwitcher> ContentSwitcher;

	/** 当前激活 Tab 索引 */
	int32 ActiveTabIndex = 0;
};