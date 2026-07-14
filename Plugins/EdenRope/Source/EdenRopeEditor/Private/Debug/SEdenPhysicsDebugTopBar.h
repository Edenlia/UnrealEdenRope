// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UEdenPhysicsDebugSubsystem;

/**
 * Eden Physics Debug 顶部工具行
 *
 * 从左到右：
 *   [Auto Timestepping Enabled/Disabled] | Manually Timestep  Delta Time: [0.001666]  [Step Once]   ...   Solver ●
 *
 * 状态通过 UEdenPhysicsDebugSubsystem 读取，并订阅其事件实时刷新
 */
class SEdenPhysicsDebugTopBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEdenPhysicsDebugTopBar) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SEdenPhysicsDebugTopBar();

private:
	/** 获取 Subsystem（可能为 nullptr，若编辑器未就绪） */
	UEdenPhysicsDebugSubsystem* GetSubsystem() const;

	// ===== UI 绑定回调 =====

	FText             GetAutoTimesteppingButtonText() const;
	FReply            OnAutoTimesteppingClicked();

	TOptional<float>  GetManualDeltaTimeValue() const;
	void              OnManualDeltaTimeCommitted(float NewValue, ETextCommit::Type CommitType);

	bool              IsStepOnceEnabled() const;
	FReply            OnStepOnceClicked();

	FSlateColor       GetSolverIndicatorColor() const;
	FText             GetSolverIndicatorTooltip() const;

private:
	/** 订阅 Subsystem 事件的句柄 */
	FDelegateHandle SolverAttachedHandle;
	FDelegateHandle SolverDetachedHandle;
	FDelegateHandle AutoTimesteppingChangedHandle;
};

