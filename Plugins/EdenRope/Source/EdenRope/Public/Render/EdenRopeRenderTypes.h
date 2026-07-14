// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenRopeRenderTypes.generated.h"

UENUM(BlueprintType)
enum class ERopeRenderMode : uint8
{
	Tube        UMETA(DisplayName = "Tube"),
	SegmentMesh UMETA(DisplayName = "Segment Mesh"),
};

UENUM(BlueprintType)
enum class ESegmentMeshForwardAxis : uint8
{
	X  UMETA(DisplayName = "+X"),
	Y  UMETA(DisplayName = "+Y"),
	Z  UMETA(DisplayName = "+Z"),
};
