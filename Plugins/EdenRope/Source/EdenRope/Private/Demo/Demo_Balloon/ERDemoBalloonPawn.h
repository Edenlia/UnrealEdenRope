// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InputMappingContext.h"
#include "GameFramework/Pawn.h"
#include "ERDemoBalloonPawn.generated.h"

class UStaticMeshComponent;
class UEdenPinComponent;
class UEdenRopeComponent;

UCLASS()
class EDENROPE_API AERDemoBalloonPawn : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	AERDemoBalloonPawn();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BalloonMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UEdenPinComponent> RopePin;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UEdenRopeComponent> RopeComponent;

	// Target height above the floor to maintain (cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy")
	float NeutralHeight = 300.f;

	// Force strength applied per cm of height error
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy")
	float HeightForceStrength = 100.f;

	// Read-only: current distance from balloon to floor this frame (cm)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Buoyancy")
	float HeightToFloor = 0.f;

	UFUNCTION(BlueprintCallable, Category = "Buoyancy")
	void SetNeutralHeight(float NewNeutralHeight);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Input")
	void OnLook(const FInputActionValue& Value);
	virtual void OnLook_Implementation(const FInputActionValue& Value);

private:
	void Input_Look(const FInputActionValue& Value);
};
