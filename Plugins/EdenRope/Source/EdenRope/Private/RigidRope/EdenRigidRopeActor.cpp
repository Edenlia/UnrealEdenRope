// Fill out your copyright notice in the Description page of Project Settings.

#include "RigidRope/EdenRigidRopeActor.h"
#include "RigidRope/EdenRigidRopeComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EdenRigidRopeActor)


AEdenRigidRopeActor::AEdenRigidRopeActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RopeComponent = CreateDefaultSubobject<UEdenRigidRopeComponent>(TEXT("RopeComponent0"));
	RootComponent = RopeComponent;
}

