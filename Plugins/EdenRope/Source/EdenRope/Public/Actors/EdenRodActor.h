// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/EdenRodComponent.h"
#include "EdenRodActor.generated.h"

UCLASS(BlueprintType, Blueprintable, ComponentWrapperClass)
class EDENROPE_API AEdenRodActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AEdenRodActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Cable component that performs simulation and rendering */
	UPROPERTY(Category = "EdenRod", VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UEdenRodComponent> EdenRodComponent;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};
