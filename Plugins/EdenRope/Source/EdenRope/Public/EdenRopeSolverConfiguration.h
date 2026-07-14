// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenRopeSolverConfiguration.generated.h"

/**
 * EdenRope 求解器配置结构
 * 仿照 FChaosSolverConfiguration 模式，集中管理所有求解器参数
 */
USTRUCT(BlueprintType)
struct EDENROPE_API FEdenRopeSolverConfiguration
{
	GENERATED_BODY()

	// ===== Framerate =====

	/**
	 * 最大物理 DeltaTime（秒）
	 * 超过此值的 DeltaTime 会被钳制，防止失去焦点后恢复时物理爆炸
	 */
	UPROPERTY(config, EditAnywhere, Category = "Framerate", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float MaxPhysicsDeltaTime = 1.0f / 30.0f;

	/** 启用固定时间步模拟。开启后物理以 FixedDeltaTime 恒定步长运行，渲染使用插值平滑 */
	UPROPERTY(config, EditAnywhere, Category = "Framerate")
	bool bUseFixedTimestep = true;

	/** 固定物理步长（秒）。仅在 bUseFixedTimestep 为 true 时生效。默认 1/60s */
	UPROPERTY(config, EditAnywhere, Category = "Framerate",
		meta = (ClampMin = "0.001", ClampMax = "0.1", EditCondition = "bUseFixedTimestep"))
	float FixedDeltaTime = 1.0f / 60.0f;

	/** 每渲染帧最大物理子步数。防止低帧率时 spiral-of-death。超出部分的时间会被丢弃 */
	UPROPERTY(config, EditAnywhere, Category = "Framerate",
		meta = (ClampMin = "1", ClampMax = "16", EditCondition = "bUseFixedTimestep"))
	int32 MaxSubsteps = 8;

	// ===== Self Collision =====

	/** 碰撞检测边距（m），segment 间距离 < rA + rB + margin 时生成约束 */
	UPROPERTY(config, EditAnywhere, Category = "Collision|SelfCollision", meta = (ClampMin = "0.0", ClampMax = "0.1"))
	float SelfCollisionMargin = 0.01f;

	// ===== Solver =====

	/** 约束求解器迭代次数 */
	UPROPERTY(config, EditAnywhere, Category = "Solver", meta = (ClampMin = "1", ClampMax = "32"))
	int32 SolverIterations = 3;

	/** 全局速度阻尼 [0, 1]，每秒损失的速度百分比。0 = 无阻尼，1 = 完全停止。帧率无关。 */
	UPROPERTY(config, EditAnywhere, Category = "Solver|Damping", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GlobalDamping = 0.0f;
};
