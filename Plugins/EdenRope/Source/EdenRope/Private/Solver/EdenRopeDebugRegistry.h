// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Handles/EdenRopeHandle.h"

/**
 * 约束类型（仅用于 Debug Registry 展示/索引，非运行时分类）
 */
enum class EEdenConstraintKind : uint8
{
	Distance = 0,
	IsoBend,
	ProjBend,
	Pin,
	StretchShear,
	BendTwist,
	Count
};

/**
 * 约束引用（UI 侧展示用的轻量 POD）
 */
struct FEdenConstraintRef
{
	EEdenConstraintKind Kind = EEdenConstraintKind::Distance;
	int32 IndexInKindArray = INDEX_NONE;
	FString DisplayName;
};

/**
 * Evolution 调试命名注册中心（仅 Editor 构建）
 *
 * 记录：
 *  - 每个 Component（一根绳索，FEdenRopeHandle）对应的 DisplayName（来自 Component 的 GetReadableName()）
 *  - 每个粒子 / 段的显示名（"{DisplayName}_Particle{X}" / "{DisplayName}_Segment{Y}"，前缀为 Component DisplayName）
 *  - 所有约束的显示名（"Constraint{Z}"）与其参与的粒子/段全局索引
 *  - 反向索引：Particle/Segment 全局索引 → 约束引用列表
 *
 * 所有名称与索引都按"全局索引"对齐，Unregister 时只墓碑不迁移。
 * 根节点语义 = 一个 Component（一根绳索），一个 Handle，一个 RopeEntry。
 */
class EDENROPE_API FEdenRopeDebugRegistry
{
public:
	/** Rope Registry 条目（根节点） */
	struct FRopeEntry
	{
		FEdenRopeHandle Handle;
		/** Component 的 DisplayName（来自 UObject::GetReadableName()，通常形如 "OwnerActor.ComponentName"） */
		FString DisplayName;
		/** 段全局起点：Rod 使用 SegmentOrientationStartIndex；Rope 使用 ParticleStartIndex 作为占位（同 Handle 内单调） */
		int32 SegmentGlobalStart = INDEX_NONE;
		/** 段数量 = ParticleCount - 1（Rope 与 Rod 统一） */
		int32 SegmentCount = 0;
		bool bValid = false;
	};

	/** 约束条目（按类型分组，DisplayName 使用全局 NextSerial） */
	struct FConstraintEntry
	{
		FString DisplayName;
		TArray<int32> ParticleGlobalIndices;
		TArray<int32> SegmentGlobalIndices;
	};

	// ===== 注册入口（由 Evolution 调用） =====

	/**
	 * 预注册一根绳索：创建 RopeEntry（Label 先空），并把 ParticleNames / SegmentNames 扩容到对应范围（占位空串）
	 * @param Handle              已填充粒子/段全局范围的 Handle
	 * @param SegmentGlobalStart  段全局起点：Rod 用 SegmentOrientationStartIndex；Rope 用 ParticleStartIndex 占位
	 * @return 在 RopeEntries 中的索引
	 */
	int32 BeginRegisterRope(const FEdenRopeHandle& Handle, int32 SegmentGlobalStart);

	/**
	 * 由 Component 上报的 DisplayName 补齐绑定：填充 RopeEntry.DisplayName 与对应的 ParticleNames / SegmentNames
	 * 若未找到匹配 Handle 条目则忽略
	 */
	void AssignDisplayName(const FEdenRopeHandle& Handle, const FString& InDisplayName);

	/** 注销：将 RopeEntry 标记为 invalid，保留所有名称与约束条目（墓碑） */
	void UnregisterRope(const FEdenRopeHandle& Handle);

	/**
	 * 注册一个约束：生成 DisplayName、写入对应 Kind 数组，并把引用推入每个参与粒子/段的反向索引
	 * @return 该约束在其 Kind 数组中的索引
	 */
	int32 RegisterConstraint(
		EEdenConstraintKind Kind,
		TArrayView<const int32> ParticleGlobalIndices,
		TArrayView<const int32> SegmentGlobalIndices = TArrayView<const int32>());

	/** 清空整个 Registry（Evolution 销毁时调用） */
	void Reset();

	// ===== 查询 API（UI 调用） =====

	FString GetParticleName(int32 GlobalParticleIndex) const;
	FString GetSegmentName(int32 GlobalSegmentIndex) const;
	FString GetConstraintName(EEdenConstraintKind Kind, int32 IndexInKindArray) const;

	TArray<FEdenConstraintRef> GetConstraintsForParticle(int32 GlobalParticleIndex) const;
	TArray<FEdenConstraintRef> GetConstraintsForSegment(int32 GlobalSegmentIndex) const;

	const TArray<FRopeEntry>& GetRopeEntries() const { return RopeEntries; }

	/** Kind → 字符串（供 UI 显示） */
	static const TCHAR* KindToString(EEdenConstraintKind Kind);

private:
	/** 查找 Handle 在 RopeEntries 中的索引（匹配 ParticleStartIndex 即认为同一条） */
	int32 FindRopeEntryIndex(const FEdenRopeHandle& Handle) const;

private:
	TArray<FRopeEntry> RopeEntries;

	/** 按全局粒子索引对齐的粒子名数组（Unregister 时不回收） */
	TArray<FString> ParticleNames;

	/** 按全局段索引对齐的段名数组（Unregister 时不回收） */
	TArray<FString> SegmentNames;

	/** 按 Kind 分类的约束条目（索引 = IndexInKindArray） */
	TArray<FConstraintEntry> ConstraintsByKind[(uint8)EEdenConstraintKind::Count];

	/** 反向索引：粒子全局索引 → 约束引用列表 */
	TMap<int32, TArray<FEdenConstraintRef>> ParticleToConstraints;

	/** 反向索引：段全局索引 → 约束引用列表 */
	TMap<int32, TArray<FEdenConstraintRef>> SegmentToConstraints;

	/** 所有约束共用的单调递增序号（保证 "Constraint{Z}" 不重号） */
	int32 NextSerial = 0;
};

#endif // WITH_EDITOR
