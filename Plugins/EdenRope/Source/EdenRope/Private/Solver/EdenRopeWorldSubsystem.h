// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EdenRopeSolverActor.h"
#include "EdenRopeWorldSubsystem.generated.h"

UCLASS()
class EDENROPE_API UEdenRopeWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// UTickableWorldSubsystem interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** 获取 Solver 实例 */
	AEdenRopeSolverActor* GetSolver() const { return Solver.Get(); }

	/** 快速获取当前 World 的 RopeSubsystem */
	static UEdenRopeWorldSubsystem* Get(const UWorld* World);

private:
	/** 绳索物理求解器 */
	UPROPERTY(Transient)
	TObjectPtr<AEdenRopeSolverActor> Solver;
};
