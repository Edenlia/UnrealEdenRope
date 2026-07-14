// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/EdenPinActor.h"

#if WITH_EDITORONLY_DATA
#include "Components/BillboardComponent.h"
#include "UObject/ConstructorHelpers.h"
#endif

AEdenPinActor::AEdenPinActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 创建 Pin 组件
	RopePinComponent = CreateDefaultSubobject<UEdenPinComponent>(TEXT("RopePinComponent0"));
	RootComponent = RopePinComponent;
}
