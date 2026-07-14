// Copyright Eden Games. All Rights Reserved.

#include "EdenRopeSettings.h"
#include "Solver/EdenRopeSolverActor.h"

UEdenRopeSettings::UEdenRopeSettings()
{
	// 默认使用基础 SolverActor 类
	SolverActorClass = AEdenRopeSolverActor::StaticClass();
}

const UEdenRopeSettings* UEdenRopeSettings::Get()
{
	return GetDefault<UEdenRopeSettings>();
}
