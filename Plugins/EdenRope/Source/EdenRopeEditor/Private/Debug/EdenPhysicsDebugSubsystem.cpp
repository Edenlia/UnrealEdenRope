// Copyright Eden Games. All Rights Reserved.

#include "EdenPhysicsDebugSubsystem.h"
#include "Solver/EdenRopeSolverActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogEdenPhysicsDebug, Log, All);

void UEdenPhysicsDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 订阅 Solver 生命周期事件
	SolverCreatedHandle = AEdenRopeSolverActor::OnEdenRopeSolverCreated.AddUObject(
		this, &UEdenPhysicsDebugSubsystem::HandleSolverCreated);
	SolverDestroyedHandle = AEdenRopeSolverActor::OnEdenRopeSolverDestroyed.AddUObject(
		this, &UEdenPhysicsDebugSubsystem::HandleSolverDestroyed);

	UE_LOG(LogEdenPhysicsDebug, Log, TEXT("UEdenPhysicsDebugSubsystem initialized."));
}

void UEdenPhysicsDebugSubsystem::Deinitialize()
{
	// 反注册订阅，避免 dangling delegate
	if (SolverCreatedHandle.IsValid())
	{
		AEdenRopeSolverActor::OnEdenRopeSolverCreated.Remove(SolverCreatedHandle);
		SolverCreatedHandle.Reset();
	}
	if (SolverDestroyedHandle.IsValid())
	{
		AEdenRopeSolverActor::OnEdenRopeSolverDestroyed.Remove(SolverDestroyedHandle);
		SolverDestroyedHandle.Reset();
	}

	// 清空弱引用
	Solver.Reset();

	UE_LOG(LogEdenPhysicsDebug, Log, TEXT("UEdenPhysicsDebugSubsystem deinitialized."));

	Super::Deinitialize();
}

void UEdenPhysicsDebugSubsystem::HandleSolverCreated(AEdenRopeSolverActor* InSolver)
{
	if (!InSolver)
	{
		return;
	}

	// "first wins" 策略：已有 Attach 的 Solver 则忽略后续
	if (Solver.IsValid())
	{
		UE_LOG(LogEdenPhysicsDebug, Warning,
			TEXT("Another EdenRopeSolver created while one is already attached; ignoring new solver: %s"),
			*InSolver->GetName());
		return;
	}

	Solver = InSolver;

	// 将当前偏好值应用到 Solver
	InSolver->bAutoSimulate = bAutoTimestepping;

	UE_LOG(LogEdenPhysicsDebug, Log, TEXT("EdenRopeSolver attached: %s (bAutoSimulate=%d)"),
		*InSolver->GetName(), bAutoTimestepping ? 1 : 0);

	OnSolverAttached.Broadcast();
}

void UEdenPhysicsDebugSubsystem::HandleSolverDestroyed(AEdenRopeSolverActor* InSolver)
{
	// 仅处理当前 Attach 的 Solver
	if (!Solver.IsValid() || Solver.Get() != InSolver)
	{
		return;
	}

	Solver.Reset();

	UE_LOG(LogEdenPhysicsDebug, Log, TEXT("EdenRopeSolver detached: %s"),
		InSolver ? *InSolver->GetName() : TEXT("<null>"));

	OnSolverDetached.Broadcast();
}

bool UEdenPhysicsDebugSubsystem::ToggleAutoTimestepping()
{
	bAutoTimestepping = !bAutoTimestepping;

	// 若当前 Solver 有效，同步写入
	if (AEdenRopeSolverActor* SolverPtr = Solver.Get())
	{
		SolverPtr->bAutoSimulate = bAutoTimestepping;
	}

	OnAutoTimesteppingChanged.Broadcast();

	return bAutoTimestepping;
}

float UEdenPhysicsDebugSubsystem::SetManualDeltaTime(float InDeltaTime)
{
	// 钳制到 (KINDA_SMALL_NUMBER, 1.0f]
	ManualDeltaTime = FMath::Clamp(InDeltaTime, KINDA_SMALL_NUMBER, 1.0f);
	return ManualDeltaTime;
}

bool UEdenPhysicsDebugSubsystem::RequestStepOnce()
{
	AEdenRopeSolverActor* SolverPtr = Solver.Get();
	if (!SolverPtr)
	{
		UE_LOG(LogEdenPhysicsDebug, Warning, TEXT("RequestStepOnce ignored: no solver attached."));
		return false;
	}
	if (bAutoTimestepping)
	{
		UE_LOG(LogEdenPhysicsDebug, Warning, TEXT("RequestStepOnce ignored: auto timestepping is enabled."));
		return false;
	}

	SolverPtr->DebugSimulateOneStep(ManualDeltaTime);
	UE_LOG(LogEdenPhysicsDebug, Verbose, TEXT("Stepped once with DeltaTime=%.5f"), ManualDeltaTime);
	return true;
}

