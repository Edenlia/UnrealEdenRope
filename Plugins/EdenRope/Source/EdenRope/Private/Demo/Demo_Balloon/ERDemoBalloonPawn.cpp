// Fill out your copyright notice in the Description page of Project Settings.


#include "ERDemoBalloonPawn.h"

#include "EnhancedInputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/EdenRopeComponent.h"
#include "Components/EdenPinComponent.h"


// Sets default values
AERDemoBalloonPawn::AERDemoBalloonPawn()
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Root: 气球主体 StaticMesh
	BalloonMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BalloonMesh"));
	SetRootComponent(BalloonMesh);

	// Rope: 悬挂在气球下方
	RopeComponent = CreateDefaultSubobject<UEdenRopeComponent>(TEXT("RopeComponent"));
	RopeComponent->SetupAttachment(BalloonMesh);

	// Pin: 将绳索第一个粒子固定到气球 StaticMesh
	RopePin = CreateDefaultSubobject<UEdenPinComponent>(TEXT("RopePin"));
	RopePin->SetupAttachment(BalloonMesh);
	RopePin->PinnedRopeComponentName = TEXT("RopeComponent");
	RopePin->PinnedParticleIndex = 0;
	RopePin->AttachmentMode = ERopePinAttachmentMode::AttachToComponent;
	RopePin->AttachedComponentName = TEXT("BalloonMesh");
}

// Called when the game starts or when spawned
void AERDemoBalloonPawn::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void AERDemoBalloonPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!BalloonMesh)
	{
		return;
	}

	// Raycast downward from balloon to find floor Z
	const FVector BalloonLoc = BalloonMesh->GetComponentLocation();
	const FVector TraceEnd = BalloonLoc - FVector(0.f, 0.f, 1000000.f);

	FHitResult HitResult;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, BalloonLoc, TraceEnd, ECC_WorldStatic);
	const float FloorZ = bHit ? HitResult.Location.Z : 0.f;

	HeightToFloor = BalloonLoc.Z - FloorZ;

	// Positive error = too high, apply downward force; negative = too low, apply upward force
	const float HeightError = HeightToFloor - NeutralHeight;
	BalloonMesh->AddForce(FVector(0.f, 0.f, -HeightError * HeightForceStrength));
}

void AERDemoBalloonPawn::SetNeutralHeight(float NewNeutralHeight)
{
	NeutralHeight = NewNeutralHeight;
}

// Called to bind functionality to input
void AERDemoBalloonPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		return;
	}

	if (LookAction)
	{
		EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AERDemoBalloonPawn::Input_Look);
	}
}

void AERDemoBalloonPawn::Input_Look(const FInputActionValue& Value)
{
	OnLook(Value);
}

void AERDemoBalloonPawn::OnLook_Implementation(const FInputActionValue& Value)
{
}
