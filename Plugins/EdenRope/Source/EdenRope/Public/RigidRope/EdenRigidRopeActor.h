// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameFramework/Actor.h"
#include "EdenRigidRopeActor.generated.h"

/** An actor that renders a rope with physics simulation */
UCLASS(hidecategories=(Input,Replication), showcategories=("Input|MouseInput", "Input|TouchInput"))
class EDENROPE_API AEdenRigidRopeActor : public AActor
{
	GENERATED_UCLASS_BODY()

	/** Rope component that performs simulation and rendering */
	UPROPERTY(Category=Rope, VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UEdenRigidRopeComponent> RopeComponent;
};

