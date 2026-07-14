// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UObject/WeakObjectPtr.h"
#include "EdenPhysicsDebugSubsystem.generated.h"

class AEdenRopeSolverActor;

/**
 * Eden Physics Debug 的状态集中管理子系统（类似 MVVM 的 Model 层）
 *
 * 职责：
 *  1. 监听 AEdenRopeSolverActor 的生命周期事件，维护唯一的 Solver 弱引用
 *  2. 缓存并暴露 UI 状态（bAutoTimestepping / ManualDeltaTime）
 *  3. 对外提供 Toggle / SetManualDeltaTime / RequestStepOnce 等入口
 *  4. 通过事件驱动 UI 刷新，避免 UI 主动轮询
 */
UCLASS()
class UEdenPhysicsDebugSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// ===== 对外事件 =====

	DECLARE_MULTICAST_DELEGATE(FOnSolverAttachmentChanged);
	DECLARE_MULTICAST_DELEGATE(FOnAutoTimesteppingChanged);

	/** Solver 与 Subsystem 建立关联（PIE 开始后 Solver 上线） */
	FOnSolverAttachmentChanged OnSolverAttached;

	/** Solver 与 Subsystem 解除关联（PIE 结束） */
	FOnSolverAttachmentChanged OnSolverDetached;

	/** Auto Timestepping 偏好值被切换 */
	FOnAutoTimesteppingChanged OnAutoTimesteppingChanged;

	// ===== UEditorSubsystem =====

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ===== Getter =====

	/** 当前是否有已 Attach 的 Solver */
	bool IsSolverAttached() const { return Solver.IsValid(); }

	/** 获取当前 Attach 的 Solver（可能为 nullptr） */
	AEdenRopeSolverActor* GetSolver() const { return Solver.Get(); }

	/** 当前 Auto Timestepping 偏好值 */
	bool IsAutoTimesteppingEnabled() const { return bAutoTimestepping; }

	/** 当前手动步长（秒） */
	float GetManualDeltaTime() const { return ManualDeltaTime; }

	// ===== UI 入口 =====

	/**
	 * 翻转 Auto Timestepping 偏好值
	 * 若当前 Solver 有效，同步写入 Solver->bAutoSimulate
	 * @return 翻转后的新值
	 */
	bool ToggleAutoTimestepping();

	/**
	 * 设置手动步长，内部钳制到 (KINDA_SMALL_NUMBER, 1.0f]
	 * @return 钳制后的最终值
	 */
	float SetManualDeltaTime(float InDeltaTime);

	/**
	 * 请求执行一步 Debug 模拟
	 * 仅在 Attach 成功 且 bAutoTimestepping == false 时生效
	 * @return 是否真正执行
	 */
	bool RequestStepOnce();

private:
	// ===== Solver 生命周期回调 =====

	void HandleSolverCreated(AEdenRopeSolverActor* InSolver);
	void HandleSolverDestroyed(AEdenRopeSolverActor* InSolver);

private:
	/** 当前 Attach 的 Solver 弱引用 */
	TWeakObjectPtr<AEdenRopeSolverActor> Solver;

	/** Auto Timestepping 偏好值，默认开启 */
	bool bAutoTimestepping = true;

	/** 手动步长，默认 1/60s（0.001666） */
	float ManualDeltaTime = 0.001666f;

	/** Solver 生命周期事件订阅句柄 */
	FDelegateHandle SolverCreatedHandle;
	FDelegateHandle SolverDestroyedHandle;
};

