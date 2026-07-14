// Fill out your copyright notice in the Description page of Project Settings.

#include "ERDemoPlayerControllerBase.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

// ============================================================
//  Lifecycle
// ============================================================

void AERDemoPlayerControllerBase::BeginPlay()
{
	Super::BeginPlay();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (DefaultMappingContext)
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void AERDemoPlayerControllerBase::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		return;
	}

	if (MoveAction)
	{
		EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AERDemoPlayerControllerBase::Input_Move);
	}
	if (LeftClickAction)
	{
		EIC->BindAction(LeftClickAction, ETriggerEvent::Started, this, &AERDemoPlayerControllerBase::Input_LeftClick);
	}
	if (RightClickAction)
	{
		EIC->BindAction(RightClickAction, ETriggerEvent::Started, this, &AERDemoPlayerControllerBase::Input_RightClick);
	}
}

// ============================================================
//  Private binding stubs — route to BlueprintNativeEvent thunks
// ============================================================

void AERDemoPlayerControllerBase::Input_Move(const FInputActionValue& Value)
{
	OnMove(Value);
}

void AERDemoPlayerControllerBase::Input_LeftClick(const FInputActionValue& Value)
{
	OnLeftClick(Value);
}

void AERDemoPlayerControllerBase::Input_RightClick(const FInputActionValue& Value)
{
	OnRightClick(Value);
}

// ============================================================
//  BlueprintNativeEvent default implementations (empty)
// ============================================================

void AERDemoPlayerControllerBase::OnMove_Implementation(const FInputActionValue& Value)
{
}

void AERDemoPlayerControllerBase::OnLeftClick_Implementation(const FInputActionValue& Value)
{
}

void AERDemoPlayerControllerBase::OnRightClick_Implementation(const FInputActionValue& Value)
{
}
