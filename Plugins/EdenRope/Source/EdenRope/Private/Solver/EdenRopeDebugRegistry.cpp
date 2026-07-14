// Copyright Eden Games. All Rights Reserved.

#include "EdenRopeDebugRegistry.h"

#if WITH_EDITOR

int32 FEdenRopeDebugRegistry::FindRopeEntryIndex(const FEdenRopeHandle& Handle) const
{
	if (Handle.ParticleStartIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < RopeEntries.Num(); ++i)
	{
		// 用 ParticleStartIndex 作为身份标识（注册时唯一）
		if (RopeEntries[i].Handle.ParticleStartIndex == Handle.ParticleStartIndex
			&& RopeEntries[i].Handle.ParticleCount == Handle.ParticleCount)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 FEdenRopeDebugRegistry::BeginRegisterRope(const FEdenRopeHandle& Handle, int32 SegmentGlobalStart)
{
	if (Handle.ParticleStartIndex == INDEX_NONE || Handle.ParticleCount <= 0)
	{
		return INDEX_NONE;
	}

	FRopeEntry Entry;
	Entry.Handle = Handle;
	Entry.DisplayName = FString();
	Entry.SegmentGlobalStart = SegmentGlobalStart;
	Entry.SegmentCount = FMath::Max(0, Handle.ParticleCount - 1);
	Entry.bValid = true;

	const int32 EntryIdx = RopeEntries.Add(Entry);

	// 扩容粒子名数组到覆盖该 Handle 范围（空串占位）
	const int32 ParticleEnd = Handle.ParticleStartIndex + Handle.ParticleCount;
	if (ParticleNames.Num() < ParticleEnd)
	{
		ParticleNames.SetNum(ParticleEnd);
	}

	// 扩容段名数组
	if (Entry.SegmentCount > 0 && SegmentGlobalStart >= 0)
	{
		const int32 SegEnd = SegmentGlobalStart + Entry.SegmentCount;
		if (SegmentNames.Num() < SegEnd)
		{
			SegmentNames.SetNum(SegEnd);
		}
	}

	return EntryIdx;
}

void FEdenRopeDebugRegistry::AssignDisplayName(const FEdenRopeHandle& Handle, const FString& InDisplayName)
{
	const int32 EntryIdx = FindRopeEntryIndex(Handle);
	if (EntryIdx == INDEX_NONE)
	{
		return;
	}

	FRopeEntry& Entry = RopeEntries[EntryIdx];
	Entry.DisplayName = InDisplayName;

	// 填充粒子名
	for (int32 i = 0; i < Entry.Handle.ParticleCount; ++i)
	{
		const int32 GlobalIdx = Entry.Handle.ParticleStartIndex + i;
		if (ParticleNames.IsValidIndex(GlobalIdx))
		{
			ParticleNames[GlobalIdx] = FString::Printf(TEXT("%s_Particle%d"), *InDisplayName, i);
		}
	}

	// 填充段名
	for (int32 j = 0; j < Entry.SegmentCount; ++j)
	{
		const int32 SegGlobalIdx = Entry.SegmentGlobalStart + j;
		if (SegmentNames.IsValidIndex(SegGlobalIdx))
		{
			SegmentNames[SegGlobalIdx] = FString::Printf(TEXT("%s_Segment%d"), *InDisplayName, j);
		}
	}
}

void FEdenRopeDebugRegistry::UnregisterRope(const FEdenRopeHandle& Handle)
{
	const int32 EntryIdx = FindRopeEntryIndex(Handle);
	if (EntryIdx != INDEX_NONE)
	{
		RopeEntries[EntryIdx].bValid = false;
	}
}

int32 FEdenRopeDebugRegistry::RegisterConstraint(
	EEdenConstraintKind Kind,
	TArrayView<const int32> ParticleGlobalIndices,
	TArrayView<const int32> SegmentGlobalIndices)
{
	const uint8 KindIdx = (uint8)Kind;
	if (KindIdx >= (uint8)EEdenConstraintKind::Count)
	{
		return INDEX_NONE;
	}

	FConstraintEntry Entry;
	Entry.DisplayName = FString::Printf(TEXT("Constraint%d"), NextSerial++);
	Entry.ParticleGlobalIndices.Append(ParticleGlobalIndices.GetData(), ParticleGlobalIndices.Num());
	Entry.SegmentGlobalIndices.Append(SegmentGlobalIndices.GetData(), SegmentGlobalIndices.Num());

	TArray<FConstraintEntry>& KindArray = ConstraintsByKind[KindIdx];
	const int32 IndexInKindArray = KindArray.Add(Entry);

	// 构造反向索引引用
	FEdenConstraintRef Ref;
	Ref.Kind = Kind;
	Ref.IndexInKindArray = IndexInKindArray;
	Ref.DisplayName = Entry.DisplayName;

	for (int32 PIdx : Entry.ParticleGlobalIndices)
	{
		if (PIdx < 0) continue;
		ParticleToConstraints.FindOrAdd(PIdx).Add(Ref);
	}
	for (int32 SIdx : Entry.SegmentGlobalIndices)
	{
		if (SIdx < 0) continue;
		SegmentToConstraints.FindOrAdd(SIdx).Add(Ref);
	}

	return IndexInKindArray;
}

void FEdenRopeDebugRegistry::Reset()
{
	RopeEntries.Reset();
	ParticleNames.Reset();
	SegmentNames.Reset();
	for (uint8 i = 0; i < (uint8)EEdenConstraintKind::Count; ++i)
	{
		ConstraintsByKind[i].Reset();
	}
	ParticleToConstraints.Reset();
	SegmentToConstraints.Reset();
	NextSerial = 0;
}

FString FEdenRopeDebugRegistry::GetParticleName(int32 GlobalParticleIndex) const
{
	if (!ParticleNames.IsValidIndex(GlobalParticleIndex))
	{
		return FString();
	}
	return ParticleNames[GlobalParticleIndex];
}

FString FEdenRopeDebugRegistry::GetSegmentName(int32 GlobalSegmentIndex) const
{
	if (!SegmentNames.IsValidIndex(GlobalSegmentIndex))
	{
		return FString();
	}
	return SegmentNames[GlobalSegmentIndex];
}

FString FEdenRopeDebugRegistry::GetConstraintName(EEdenConstraintKind Kind, int32 IndexInKindArray) const
{
	const uint8 KindIdx = (uint8)Kind;
	if (KindIdx >= (uint8)EEdenConstraintKind::Count)
	{
		return FString();
	}
	const TArray<FConstraintEntry>& KindArray = ConstraintsByKind[KindIdx];
	if (!KindArray.IsValidIndex(IndexInKindArray))
	{
		return FString();
	}
	return KindArray[IndexInKindArray].DisplayName;
}

TArray<FEdenConstraintRef> FEdenRopeDebugRegistry::GetConstraintsForParticle(int32 GlobalParticleIndex) const
{
	const TArray<FEdenConstraintRef>* Found = ParticleToConstraints.Find(GlobalParticleIndex);
	if (!Found)
	{
		return TArray<FEdenConstraintRef>();
	}
	return *Found;
}

TArray<FEdenConstraintRef> FEdenRopeDebugRegistry::GetConstraintsForSegment(int32 GlobalSegmentIndex) const
{
	const TArray<FEdenConstraintRef>* Found = SegmentToConstraints.Find(GlobalSegmentIndex);
	if (!Found)
	{
		return TArray<FEdenConstraintRef>();
	}
	return *Found;
}

const TCHAR* FEdenRopeDebugRegistry::KindToString(EEdenConstraintKind Kind)
{
	switch (Kind)
	{
	case EEdenConstraintKind::Distance:        return TEXT("Distance");
	case EEdenConstraintKind::IsoBend:         return TEXT("IsoBend");
	case EEdenConstraintKind::ProjBend:        return TEXT("ProjBend");
	case EEdenConstraintKind::Pin:             return TEXT("Pin");
	case EEdenConstraintKind::StretchShear:    return TEXT("StretchShear");
	case EEdenConstraintKind::BendTwist:       return TEXT("BendTwist");
	default: return TEXT("Unknown");
	}
}

#endif // WITH_EDITOR
