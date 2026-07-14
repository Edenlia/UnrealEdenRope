// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/SListView.h"
#include "Solver/EdenRopeDebugRegistry.h"

class UEdenPhysicsDebugSubsystem;
class STextBlock;

/**
 * Constraints Tab 左树节点类型
 */
enum class EEdenDebugTreeNodeKind : uint8
{
	Root = 0,
	Particle,
	Segment
};

/**
 * Constraints Tab 左树节点
 */
struct FEdenDebugTreeNode
{
	EEdenDebugTreeNodeKind Kind = EEdenDebugTreeNodeKind::Root;

	/** 粒子/段的全局索引（Root 节点为 INDEX_NONE） */
	int32 GlobalIndex = INDEX_NONE;

	/** 对应在 Registry->GetRopeEntries() 中的索引（Root 使用，子节点可继承方便重建） */
	int32 RopeEntryIndex = INDEX_NONE;

	/** 显示文本 */
	FString DisplayText;

	/** 子节点（仅 Root 有） */
	TArray<TSharedPtr<FEdenDebugTreeNode>> Children;
};

/**
 * Constraints Debug Tab 面板
 *
 * 布局：SSplitter (Horizontal)
 *   ├── [40%] 左侧 STreeView + "Refresh" 按钮（Root = RopeActor, 子 = Particle/Segment）
 *   └── [60%] 右侧 SListView（选中粒子/段参与的所有约束名）
 */
class SEdenPhysicsDebugConstraintsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEdenPhysicsDebugConstraintsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SEdenPhysicsDebugConstraintsPanel();

private:
	// ===== Tree =====
	TSharedRef<ITableRow> OnGenerateTreeRow(TSharedPtr<FEdenDebugTreeNode> Node, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetTreeChildren(TSharedPtr<FEdenDebugTreeNode> Node, TArray<TSharedPtr<FEdenDebugTreeNode>>& OutChildren);
	void OnTreeSelectionChanged(TSharedPtr<FEdenDebugTreeNode> Node, ESelectInfo::Type SelectInfo);
	FReply OnRefreshClicked();

	void RebuildTree();

	// ===== List =====
	TSharedRef<ITableRow> OnGenerateListRow(TSharedPtr<FEdenConstraintRef> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void RefreshRightList();

	// ===== Subsystem =====
	UEdenPhysicsDebugSubsystem* GetSubsystem() const;

	void HandleSolverAttached();
	void HandleSolverDetached();

	/** Text accessors for placeholder labels */
	FText GetLeftPlaceholderText() const;
	FText GetRightPlaceholderText() const;
	EVisibility GetLeftPlaceholderVisibility() const;
	EVisibility GetRightPlaceholderVisibility() const;
	EVisibility GetTreeVisibility() const;
	EVisibility GetListVisibility() const;

private:
	/** 左树数据源（Root 级别） */
	TArray<TSharedPtr<FEdenDebugTreeNode>> TreeSource;

	/** 右侧列表数据源 */
	TArray<TSharedPtr<FEdenConstraintRef>> ListSource;

	/** 当前选中节点 */
	TSharedPtr<FEdenDebugTreeNode> CurrentSelectedNode;

	TSharedPtr<STreeView<TSharedPtr<FEdenDebugTreeNode>>> TreeView;
	TSharedPtr<SListView<TSharedPtr<FEdenConstraintRef>>> ListView;

	FDelegateHandle SolverAttachedHandle;
	FDelegateHandle SolverDetachedHandle;
};
