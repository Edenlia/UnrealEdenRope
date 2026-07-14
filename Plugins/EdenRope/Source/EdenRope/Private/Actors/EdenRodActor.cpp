// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/EdenRodActor.h"

#if WITH_EDITORONLY_DATA
#include "Components/BillboardComponent.h"
#include "UObject/ConstructorHelpers.h"
#endif


// Sets default values
AEdenRodActor::AEdenRodActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	// 创建绳子组件
	EdenRodComponent = CreateDefaultSubobject<UEdenRodComponent>(TEXT("EdenRodComponent0"));
	RootComponent = EdenRodComponent;
}

// Called when the game starts or when spawned
void AEdenRodActor::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void AEdenRodActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

