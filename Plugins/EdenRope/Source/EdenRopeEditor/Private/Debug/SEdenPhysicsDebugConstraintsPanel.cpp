// Copyright Eden Games. All Rights Reserved.

#include "SEdenPhysicsDebugConstraintsPanel.h"
#include "EdenPhysicsDebugSubsystem.h"
#include "Solver/EdenRopeSolverActor.h"
#include "Solver/EdenRopeDebugRegistry.h"

#include "Editor.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SEdenPhysicsDebugConstraintsPanel"

void SEdenPhysicsDebugConstraintsPanel::Construct(const FArguments& InArgs)
{
	// 左侧：Refresh 按钮 + TreeView（或占位文本）
	TSharedRef<SVerticalBox> LeftBox = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f, 4.f, 4.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshLabel", "Refresh"))
				.ToolTipText(LOCTEXT("RefreshTooltip", "Rebuild the tree from current registry snapshot"))
				.OnClicked(this, &SEdenPhysicsDebugConstraintsPanel::OnRefreshClicked)
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f, 2.f)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FEdenDebugTreeNode>>)
				.TreeItemsSource(&TreeSource)
				.OnGenerateRow(this, &SEdenPhysicsDebugConstraintsPanel::OnGenerateTreeRow)
				.OnGetChildren(this, &SEdenPhysicsDebugConstraintsPanel::OnGetTreeChildren)
				.OnSelectionChanged(this, &SEdenPhysicsDebugConstraintsPanel::OnTreeSelectionChanged)
				.SelectionMode(ESelectionMode::Single)
				.Visibility(this, &SEdenPhysicsDebugConstraintsPanel::GetTreeVisibility)
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SEdenPhysicsDebugConstraintsPanel::GetLeftPlaceholderText)
				.Visibility(this, &SEdenPhysicsDebugConstraintsPanel::GetLeftPlaceholderVisibility)
			]
		];

	// 右侧：ListView（或占位文本）
	TSharedRef<SVerticalBox> RightBox = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f, 4.f)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				SAssignNew(ListView, SListView<TSharedPtr<FEdenConstraintRef>>)
				.ListItemsSource(&ListSource)
				.OnGenerateRow(this, &SEdenPhysicsDebugConstraintsPanel::OnGenerateListRow)
				.SelectionMode(ESelectionMode::Single)
				.Visibility(this, &SEdenPhysicsDebugConstraintsPanel::GetListVisibility)
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SEdenPhysicsDebugConstraintsPanel::GetRightPlaceholderText)
				.Visibility(this, &SEdenPhysicsDebugConstraintsPanel::GetRightPlaceholderVisibility)
			]
		];

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		+ SSplitter::Slot()
		.Value(0.4f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
			[
				LeftBox
			]
		]

		+ SSplitter::Slot()
		.Value(0.6f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
			[
				RightBox
			]
		]
	];

	// 订阅 Subsystem 事件
	if (UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem())
	{
		SolverAttachedHandle = Subsystem->OnSolverAttached.AddSP(this, &SEdenPhysicsDebugConstraintsPanel::HandleSolverAttached);
		SolverDetachedHandle = Subsystem->OnSolverDetached.AddSP(this, &SEdenPhysicsDebugConstraintsPanel::HandleSolverDetached);
	}

	// 窗口在 PIE 运行中打开时立即拉取
	RebuildTree();
	RefreshRightList();
}

SEdenPhysicsDebugConstraintsPanel::~SEdenPhysicsDebugConstraintsPanel()
{
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
	}
}

UEdenPhysicsDebugSubsystem* SEdenPhysicsDebugConstraintsPanel::GetSubsystem() const
{
	return GEditor ? GEditor->GetEditorSubsystem<UEdenPhysicsDebugSubsystem>() : nullptr;
}

void SEdenPhysicsDebugConstraintsPanel::HandleSolverAttached()
{
	RebuildTree();
	RefreshRightList();
}

void SEdenPhysicsDebugConstraintsPanel::HandleSolverDetached()
{
	TreeSource.Reset();
	ListSource.Reset();
	CurrentSelectedNode.Reset();
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
	}
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

FReply SEdenPhysicsDebugConstraintsPanel::OnRefreshClicked()
{
	RebuildTree();
	RefreshRightList();
	return FReply::Handled();
}

void SEdenPhysicsDebugConstraintsPanel::RebuildTree()
{
	TreeSource.Reset();
	CurrentSelectedNode.Reset();

	UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem();
	AEdenRopeSolverActor* Solver = Subsystem ? Subsystem->GetSolver() : nullptr;
	FEdenRopeDebugRegistry* Registry = Solver ? Solver->GetEvolutionDebugRegistry() : nullptr;

	if (!Registry)
	{
		if (TreeView.IsValid()) TreeView->RequestTreeRefresh();
		return;
	}

	const TArray<FEdenRopeDebugRegistry::FRopeEntry>& Entries = Registry->GetRopeEntries();
	for (int32 EntryIdx = 0; EntryIdx < Entries.Num(); ++EntryIdx)
	{
		const FEdenRopeDebugRegistry::FRopeEntry& Entry = Entries[EntryIdx];
		if (!Entry.bValid)
		{
			continue;
		}

		// Root 节点：显示 Component 的 DisplayName（来自 UObject::GetReadableName()）
		TSharedPtr<FEdenDebugTreeNode> RootNode = MakeShared<FEdenDebugTreeNode>();
		RootNode->Kind = EEdenDebugTreeNodeKind::Root;
		RootNode->RopeEntryIndex = EntryIdx;
		RootNode->DisplayText = Entry.DisplayName.IsEmpty() ? TEXT("<Unnamed Component>") : Entry.DisplayName;

		// 填充粒子子节点
		for (int32 i = 0; i < Entry.Handle.ParticleCount; ++i)
		{
			const int32 GlobalIdx = Entry.Handle.ParticleStartIndex + i;
			TSharedPtr<FEdenDebugTreeNode> Child = MakeShared<FEdenDebugTreeNode>();
			Child->Kind = EEdenDebugTreeNodeKind::Particle;
			Child->GlobalIndex = GlobalIdx;
			Child->RopeEntryIndex = EntryIdx;
			Child->DisplayText = Registry->GetParticleName(GlobalIdx);
			if (Child->DisplayText.IsEmpty())
			{
				Child->DisplayText = FString::Printf(TEXT("Particle[%d]"), GlobalIdx);
			}
			RootNode->Children.Add(Child);
		}

		// 填充段子节点
		for (int32 j = 0; j < Entry.SegmentCount; ++j)
		{
			const int32 SegGlobalIdx = Entry.SegmentGlobalStart + j;
			TSharedPtr<FEdenDebugTreeNode> Child = MakeShared<FEdenDebugTreeNode>();
			Child->Kind = EEdenDebugTreeNodeKind::Segment;
			Child->GlobalIndex = SegGlobalIdx;
			Child->RopeEntryIndex = EntryIdx;
			Child->DisplayText = Registry->GetSegmentName(SegGlobalIdx);
			if (Child->DisplayText.IsEmpty())
			{
				Child->DisplayText = FString::Printf(TEXT("Segment[%d]"), SegGlobalIdx);
			}
			RootNode->Children.Add(Child);
		}

		TreeSource.Add(RootNode);
	}

	if (TreeView.IsValid())
	{
		// 默认展开所有根节点，便于一眼看见粒子/段
		for (TSharedPtr<FEdenDebugTreeNode>& Root : TreeSource)
		{
			TreeView->SetItemExpansion(Root, true);
		}
		TreeView->RequestTreeRefresh();
	}
}

TSharedRef<ITableRow> SEdenPhysicsDebugConstraintsPanel::OnGenerateTreeRow(TSharedPtr<FEdenDebugTreeNode> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FEdenDebugTreeNode>>, OwnerTable)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Node.IsValid() ? Node->DisplayText : FString()))
	];
}

void SEdenPhysicsDebugConstraintsPanel::OnGetTreeChildren(TSharedPtr<FEdenDebugTreeNode> Node, TArray<TSharedPtr<FEdenDebugTreeNode>>& OutChildren)
{
	if (Node.IsValid())
	{
		OutChildren = Node->Children;
	}
}

void SEdenPhysicsDebugConstraintsPanel::OnTreeSelectionChanged(TSharedPtr<FEdenDebugTreeNode> Node, ESelectInfo::Type SelectInfo)
{
	// 本期单选：取传入的 Node，若为 Root 节点则忽略
	if (Node.IsValid() && (Node->Kind == EEdenDebugTreeNodeKind::Particle || Node->Kind == EEdenDebugTreeNodeKind::Segment))
	{
		CurrentSelectedNode = Node;
	}
	else
	{
		CurrentSelectedNode.Reset();
	}
	RefreshRightList();
}

void SEdenPhysicsDebugConstraintsPanel::RefreshRightList()
{
	ListSource.Reset();

	if (CurrentSelectedNode.IsValid())
	{
		UEdenPhysicsDebugSubsystem* Subsystem = GetSubsystem();
		AEdenRopeSolverActor* Solver = Subsystem ? Subsystem->GetSolver() : nullptr;
		FEdenRopeDebugRegistry* Registry = Solver ? Solver->GetEvolutionDebugRegistry() : nullptr;

		if (Registry)
		{
			TArray<FEdenConstraintRef> Refs;
			if (CurrentSelectedNode->Kind == EEdenDebugTreeNodeKind::Particle)
			{
				Refs = Registry->GetConstraintsForParticle(CurrentSelectedNode->GlobalIndex);
			}
			else if (CurrentSelectedNode->Kind == EEdenDebugTreeNodeKind::Segment)
			{
				Refs = Registry->GetConstraintsForSegment(CurrentSelectedNode->GlobalIndex);
			}

			ListSource.Reserve(Refs.Num());
			for (const FEdenConstraintRef& R : Refs)
			{
				ListSource.Add(MakeShared<FEdenConstraintRef>(R));
			}
		}
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SEdenPhysicsDebugConstraintsPanel::OnGenerateListRow(TSharedPtr<FEdenConstraintRef> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	FString Text;
	if (Item.IsValid())
	{
		Text = FString::Printf(TEXT("%s (%s)"), *Item->DisplayName, FEdenRopeDebugRegistry::KindToString(Item->Kind));
	}
	return SNew(STableRow<TSharedPtr<FEdenConstraintRef>>, OwnerTable)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Text))
	];
}

FText SEdenPhysicsDebugConstraintsPanel::GetLeftPlaceholderText() const
{
	return LOCTEXT("NoRopes", "(No registered ropes)");
}

FText SEdenPhysicsDebugConstraintsPanel::GetRightPlaceholderText() const
{
	if (!CurrentSelectedNode.IsValid())
	{
		return LOCTEXT("NoSelection", "(Select a particle or segment)");
	}
	return LOCTEXT("NoConstraints", "(No constraints)");
}

EVisibility SEdenPhysicsDebugConstraintsPanel::GetTreeVisibility() const
{
	return TreeSource.Num() > 0 ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SEdenPhysicsDebugConstraintsPanel::GetLeftPlaceholderVisibility() const
{
	return TreeSource.Num() > 0 ? EVisibility::Hidden : EVisibility::Visible;
}

EVisibility SEdenPhysicsDebugConstraintsPanel::GetListVisibility() const
{
	return ListSource.Num() > 0 ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SEdenPhysicsDebugConstraintsPanel::GetRightPlaceholderVisibility() const
{
	return ListSource.Num() > 0 ? EVisibility::Hidden : EVisibility::Visible;
}

#undef LOCTEXT_NAMESPACE
