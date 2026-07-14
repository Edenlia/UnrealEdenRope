// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * BroadPhase Debug Tab 内容占位面板
 * 本期仅显示 "(Coming soon)"，后续填充具体可视化与交互
 */
class SEdenPhysicsDebugBroadPhasePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEdenPhysicsDebugBroadPhasePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};

/**
 * NarrowPhase Debug Tab 内容占位面板
 * 本期仅显示 "(Coming soon)"，后续填充具体可视化与交互
 */
class SEdenPhysicsDebugNarrowPhasePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEdenPhysicsDebugNarrowPhasePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};

