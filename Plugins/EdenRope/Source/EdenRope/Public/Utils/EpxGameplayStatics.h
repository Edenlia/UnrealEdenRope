// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/EdenRopeComponentBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EpxGameplayStatics.generated.h"

/**
 * 
 */
UCLASS()
class EDENROPE_API UEpxGameplayStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category="Eden Physics")
	static bool SetRopeAllParticleMass(UEdenRopeComponentBase* RopeBaseComponent, float PerParticleMass);

	UFUNCTION(BlueprintCallable, Category="Eden Physics")
	static bool SetRopeSingleParticleMass(UEdenRopeComponentBase* RopeBaseComponent, float PerParticleMass, int ParticleIndex);
};
