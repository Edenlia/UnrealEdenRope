// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/EdenPinComponent.h"
#include "EdenPinActor.generated.h"

UCLASS(BlueprintType, Blueprintable, ComponentWrapperClass)
class EDENROPE_API AEdenPinActor : public AActor
{
	GENERATED_BODY()

public:
	AEdenPinActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Pin component that pins a rope particle to a position */
	UPROPERTY(Category = "EdenRope", VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UEdenPinComponent> RopePinComponent;
};
