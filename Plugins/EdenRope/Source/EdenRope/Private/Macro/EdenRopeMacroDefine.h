#pragma once

#include "CoreMinimal.h"

// Whether the StretchShear & BendTwist Constraint calculated in material frame
#ifndef ROD_MATERIAL_FRAME_CONSTRAINT_PROJECTION
// #define ROD_MATERIAL_FRAME_CONSTRAINT_PROJECTION 1
#define ROD_MATERIAL_FRAME_CONSTRAINT_PROJECTION 0
#endif

EDENROPE_API DECLARE_LOG_CATEGORY_EXTERN(LogEdenRope, Log, All);