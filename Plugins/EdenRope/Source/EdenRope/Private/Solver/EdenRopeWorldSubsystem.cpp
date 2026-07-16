// Copyright Eden Games. All Rights Reserved.

#include "EdenRopeWorldSubsystem.h"
#include "EdenRopeSettings.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogEdenRopeSubsystem, Log, All);

void UEdenRopeWorldSubsystem::Deinitialize()
{
	UE_LOG(LogEdenRopeSubsystem, Log, TEXT("UEdenRopeWorldSubsystem deinitializing for world: %s"),
		*GetWorld()->GetName());

	if (Solver)
	{
		Solver->Destroy();
		Solver = nullptr;
	}

	Super::Deinitialize();
}

bool UEdenRopeWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 只在游戏世界中创建（不在编辑器预览等场景创建）
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UEdenRopeWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 从 Settings 获取 SolverActorClass
	const UEdenRopeSettings* Settings = UEdenRopeSettings::Get();

	// 物理世界未启用时不创建 Solver（RigidEdenRope 不受影响）
	if (!Settings || !Settings->bEnableEdenPhysicsWorld)
	{
		UE_LOG(LogEdenRopeSubsystem, Log, TEXT("EdenPhysicsWorld disabled; skipping solver spawn for world %s"),
			*InWorld.GetName());
		return;
	}

	UClass* SolverClass = nullptr;

	if (Settings && !Settings->SolverActorClass.IsNull())
	{
		SolverClass = Settings->SolverActorClass.LoadSynchronous();
	}
	
	// 如果没有配置或加载失败，使用默认类
	if (!SolverClass)
	{
		SolverClass = AEdenRopeSolverActor::StaticClass();
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Solver = InWorld.SpawnActor<AEdenRopeSolverActor>(
		SolverClass,
		FTransform::Identity,
		Params
	);

	UE_LOG(LogEdenRopeSubsystem, Log, TEXT("UEdenRopeWorldSubsystem initialized for world: %s, SolverClass: %s"),
		*InWorld.GetName(), *SolverClass->GetName());
}

void UEdenRopeWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

TStatId UEdenRopeWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEdenRopeWorldSubsystem, STATGROUP_Tickables);
}

UEdenRopeWorldSubsystem* UEdenRopeWorldSubsystem::Get(const UWorld* World)
{
	if (World)
	{
		return World->GetSubsystem<UEdenRopeWorldSubsystem>();
	}
	return nullptr;
}
