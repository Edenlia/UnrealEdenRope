// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "ERDemoPlayerControllerBase.generated.h"

struct FInputActionValue;

/**
 * Demo player controller base class.
 * Handles Enhanced Input setup and exposes three overridable input events
 * (Move, LeftClick, RightClick) for Blueprint and C++ subclasses.
 */
UCLASS(Abstract, Blueprintable)
class EDENROPE_API AERDemoPlayerControllerBase : public APlayerController
{
	GENERATED_BODY()

public:

	// --- Input Assets (assign in Blueprint Defaults or CDO) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LeftClickAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> RightClickAction;

protected:

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	// --- Overridable Input Events ---

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Input")
	void OnMove(const FInputActionValue& Value);
	virtual void OnMove_Implementation(const FInputActionValue& Value);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Input")
	void OnLeftClick(const FInputActionValue& Value);
	virtual void OnLeftClick_Implementation(const FInputActionValue& Value);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Input")
	void OnRightClick(const FInputActionValue& Value);
	virtual void OnRightClick_Implementation(const FInputActionValue& Value);

private:

	// Private binding stubs — call the BlueprintNativeEvent thunks so BP overrides fire correctly
	void Input_Move(const FInputActionValue& Value);
	void Input_LeftClick(const FInputActionValue& Value);
	void Input_RightClick(const FInputActionValue& Value);
};
