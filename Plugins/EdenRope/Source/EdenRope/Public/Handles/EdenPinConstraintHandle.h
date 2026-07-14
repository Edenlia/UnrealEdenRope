// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenPinConstraintHandle.generated.h"

USTRUCT(BlueprintType)
struct EDENROPE_API FEdenPinConstraintHandle
{
	GENERATED_BODY()
	
	// Pinned的粒子GlobalIndex
	int32 PinnedParticleIndex = INDEX_NONE;
	int32 PinConstraintIndex = INDEX_NONE;
	
	// 是否有效（删除时标记为 false）
	bool bValid = false;

	// 检查 Handle 是否有效
	bool IsValid() const
	{
		return bValid;
	}

	// 重置 Handle
	void Reset()
	{
		PinnedParticleIndex = INDEX_NONE;
		PinConstraintIndex = INDEX_NONE;
		bValid = false;
	}
};
