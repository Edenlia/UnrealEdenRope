// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FEdenRopeModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

#if WITH_EDITOR
private:
	void OnPreBeginPIE(const bool bIsSimulating);
	void OnPostBeginPIE(const bool bIsSimulating);
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);

	FDelegateHandle PreBeginPIEHandle;
	FDelegateHandle PostBeginPIEHandle;
	FDelegateHandle PropertyChangedHandle;
#endif
};
