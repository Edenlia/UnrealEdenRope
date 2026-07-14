// Copyright Eden Games. All Rights Reserved.

#include "SEdenPhysicsDebugTopBar.h"
#include "EdenPhysicsDebugSubsystem.h"

#include "Editor.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SEdenPhysicsDebugTopBar"

namespace EdenPhysicsDebugTopBar
{
	// 圆形指示灯颜色
	static const FLinearColor SolverAttachedColor(0.1f, 0.85f, 0.1f, 1.0f);
	static const FLinearColor SolverManualColor  (0.9f, 0.8f,  0.1f, 1.0f); // Attached 但未 Auto Simulate
	static const FLinearColor SolverDetachedColor(0.85f, 0.1f, 0.1f, 1.0f);
}

void SEdenPhysicsDebugTopBar::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SHorizontalBox)

		// (a) Auto Timestepping 切换按钮
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.f, 2.f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("AutoTimesteppingTooltip", "Toggle Auto Timestepping. When disabled, you can step manually."))
			.OnClicked(this, &SEdenPhysicsDebugTopBar::OnAutoTimesteppingClicked)
			[
				SNew(STextBlock)
				.Text(this, &SEdenPhysicsDebugTopBar::GetAutoTimesteppingButtonText)
				.Margin(FMargin(6.f, 2.f))
			]
		]

		// (b) 垂直分隔线
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(6.f, 4.f)
		.VAlign(VAlign_Fill)
		[
			SNew(SSeparator)
			.Orientation(Orient_Vertical)
			.Thickness(1.f)
		]

		// (c) 静态文字 Manually Timestep
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.f, 2.f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ManuallyTimestepLabel", "Manually Timestep"))
		]

		// (d) 静态文字 Delta Time:
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(12.f, 2.f, 2.f, 2.f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DeltaTimeLabel", "Delta Time:"))
		]

		// (e) 数字输入框（5 位小数）
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.f, 2.f)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(100.f)
			[
				SNew(SNumericEntryBox<float>)
				.AllowSpin(false)
				.MinValue(KINDA_SMALL_NUMBER)
				.MaxValue(1.0f)
				.MinFractionalDigits(5)
				.MaxFractionalDigits(5)
				.Value(this, &SEdenPhysicsDebugTopBar::GetManualDeltaTimeValue)
				.OnValueCommitted(this, &SEdenPhysicsDebugTopBar::OnManualDeltaTimeCommitted)
			]
		]

		// (f) Step Once 按钮
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(8.f, 2.f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("StepOnceTooltip", "Advance the physics simulation by one Delta Time step."))
			.IsEnabled(this, &SEdenPhysicsDebugTopBar::IsStepOnceEnabled)
			.OnClicked(this, &SEdenPhysicsDebugTopBar::OnStepOnceClicked)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StepOnceLabel", "Step Once"))
				.Margin(FMargin(6.f, 2.f))
			]
		]

		// (g) 右侧填充
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(SSpacer)
		]

		// (h) 静态文字 Solver
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.f, 2.f, 0.f, 2.f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SolverLabel", "Solver"))
		]

		// (i) 圆形指示灯（参考 McpToolbarExtension 的小型样式）
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(10.f, 0.f, 10.f, 0.f)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(10.f)
			.HeightOverride(10.f)
			.ToolTipText(this, &SEdenPhysicsDebugTopBar::GetSolverIndicatorTooltip)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.FilledCircle"))
				.ColorAndOpacity(this, &SEdenPhysicsDebugTopBar::GetSolverIndicatorColor)
			]
		]
	];

	// 订阅 Subsystem 事件，刷新 UI（所有可变绑定均通过 TAttribute 读取，这里只需触发重绘）
	if (UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem())
	{
		TWeakPtr<SEdenPhysicsDebugTopBar> WeakSelf = SharedThis(this);
		auto InvalidateLambda = [WeakSelf]()
		{
			if (TSharedPtr<SEdenPhysicsDebugTopBar> Self = WeakSelf.Pin())
			{
				Self->Invalidate(EInvalidateWidgetReason::Paint);
			}
		};
		SolverAttachedHandle         = Subsystem->OnSolverAttached.AddLambda(InvalidateLambda);
		SolverDetachedHandle         = Subsystem->OnSolverDetached.AddLambda(InvalidateLambda);
		AutoTimesteppingChangedHandle = Subsystem->OnAutoTimesteppingChanged.AddLambda(InvalidateLambda);
	}
}

SEdenPhysicsDebugTopBar::~SEdenPhysicsDebugTopBar()
{
	// 反注册 Subsystem 事件，防止 dangling delegate
	if (UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem())
	{
		if (SolverAttachedHandle.IsValid())
		{
			Subsystem->OnSolverAttached.Remove(SolverAttachedHandle);
		}
		if (SolverDetachedHandle.IsValid())
		{
			Subsystem->OnSolverDetached.Remove(SolverDetachedHandle);
		}
		if (AutoTimesteppingChangedHandle.IsValid())
		{
			Subsystem->OnAutoTimesteppingChanged.Remove(AutoTimesteppingChangedHandle);
		}
	}
}

UEdenPhysicsDebugSubsystem* SEdenPhysicsDebugTopBar::GetSubsystem() const
{
	return GEditor ? GEditor->GetEditorSubsystem<UEdenPhysicsDebugSubsystem>() : nullptr;
}

FText SEdenPhysicsDebugTopBar::GetAutoTimesteppingButtonText() const
{
	const UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem();
	const bool bEnabled = Subsystem ? Subsystem->IsAutoTimesteppingEnabled() : true;
	return bEnabled
		? LOCTEXT("AutoTimesteppingEnabled", "Auto Timestepping Enabled")
		: LOCTEXT("AutoTimesteppingDisabled", "Auto Timestepping Disabled");
}

FReply SEdenPhysicsDebugTopBar::OnAutoTimesteppingClicked()
{
	if (UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->ToggleAutoTimestepping();
	}
	return FReply::Handled();
}

TOptional<float> SEdenPhysicsDebugTopBar::GetManualDeltaTimeValue() const
{
	if (const UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem())
	{
		return Subsystem->GetManualDeltaTime();
	}
	return 0.001666f;
}

void SEdenPhysicsDebugTopBar::OnManualDeltaTimeCommitted(float NewValue, ETextCommit::Type CommitType)
{
	if (UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->SetManualDeltaTime(NewValue);
	}
}

bool SEdenPhysicsDebugTopBar::IsStepOnceEnabled() const
{
	const UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return false;
	}
	return Subsystem->IsSolverAttached() && !Subsystem->IsAutoTimesteppingEnabled();
}

FReply SEdenPhysicsDebugTopBar::OnStepOnceClicked()
{
	if (UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->RequestStepOnce();
	}
	return FReply::Handled();
}

FSlateColor SEdenPhysicsDebugTopBar::GetSolverIndicatorColor() const
{
	const UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem || !Subsystem->IsSolverAttached())
	{
		// Subsystem 无效或 Solver 未 attach → 红
		return FSlateColor(EdenPhysicsDebugTopBar::SolverDetachedColor);
	}

	// 已 attach：根据 Auto Simulate (= Auto Timestepping) 状态区分绿 / 黄
	return Subsystem->IsAutoTimesteppingEnabled()
		? FSlateColor(EdenPhysicsDebugTopBar::SolverAttachedColor)
		: FSlateColor(EdenPhysicsDebugTopBar::SolverManualColor);
}

FText SEdenPhysicsDebugTopBar::GetSolverIndicatorTooltip() const
{
	const UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem || !Subsystem->IsSolverAttached())
	{
		return LOCTEXT("SolverIndicatorTooltip_NotAttached", "Solver not attached");
	}

	return Subsystem->IsAutoTimesteppingEnabled()
		? LOCTEXT("SolverIndicatorTooltip_AutoOn",  "Solver attached, Auto Simulate ON")
		: LOCTEXT("SolverIndicatorTooltip_AutoOff", "Solver attached, Auto Simulate OFF");
}

#undef LOCTEXT_NAMESPACE

