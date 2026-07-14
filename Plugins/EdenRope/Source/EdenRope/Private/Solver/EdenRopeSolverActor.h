// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenRopeSolverConfiguration.h"
#include "Handles/EdenPinConstraintHandle.h"
#include "Handles/EdenRopeHandle.h"
#include "IEdenRopeEvolution.h"

#include "EdenRopeSolverActor.generated.h"

class AEdenRopeSolverActor;

/** Solver 生命周期事件签名：创建 / 销毁均以 Solver 指针为参数 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEdenRopeSolverLifetime, AEdenRopeSolverActor*);

/**
 * 绳索物理求解器 Actor
 * 作为薄壳层，负责 Actor 生命周期和接口转发
 * 实际的物理模拟由 Evolution 执行
 *
 * 配置从 UEdenRopeSettings (Project Settings > Plugins > EdenRope) 读取
 */
UCLASS()
class EDENROPE_API AEdenRopeSolverActor : public AActor
{
	GENERATED_BODY()

public:
	AEdenRopeSolverActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// ===== 生命周期事件（供编辑器调试等外部模块订阅） =====

	/** Solver BeginPlay 完成后广播，参数为新创建的 Solver 指针 */
	static FOnEdenRopeSolverLifetime OnEdenRopeSolverCreated;

	/** Solver EndPlay 起始时广播（Evolution 释放前），参数为即将销毁的 Solver 指针 */
	static FOnEdenRopeSolverLifetime OnEdenRopeSolverDestroyed;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// ===== 接口转发 =====

	/**
	 * 注册一根绳索到 Solver
	 * @param StartPositionCm 起始位置（cm）
	 * @param Direction 初始方向（单位向量）
	 * @param Params 绳索创建参数
	 * @return Handle，用于后续访问和同步
	 */
	FEdenRopeHandle RegisterRope(
		const FVector& StartPositionCm,
		const FVector& Direction,
		const FEdenRopeCreationParams& Params
	);

	/** 从采样点列表注册绳索 */
	FEdenRopeHandle RegisterRope(
		TArrayView<const FVector> InitialPositionsCm,
		const FEdenRopeCreationParams& Params
	);

	/**
	 * 注销绳索
	 */
	void UnregisterRope(FEdenRopeHandle& Handle);

	/**
	 * 注册一根 Rod（位置+方向 POBCR 模型）
	 */
	FEdenRopeHandle RegisterRod(
		const FVector& StartPositionCm,
		const FVector& Direction,
		const FEdenRopeCreationParams& Params
	);

	/** 从采样点列表注册 Rod */
	FEdenRopeHandle RegisterRod(
		TArrayView<const FVector> InitialPositionsCm,
		TArrayView<const FQuat> InitialOrientations,
		const FEdenRopeCreationParams& Params
	);

	/**
	 * 注册 Pin 约束（固定到世界空间位置）
	 */
	FEdenPinConstraintHandle RegisterPinConstraint(
		uint32 ParticleGlobalIndex,
		const FVector& PositionCm
	);

	/**
	 * 注册 Pin 约束（连接到动态刚体）
	 * @param ParticleGlobalIndex 粒子全局索引
	 * @param Component 要连接的 PrimitiveComponent
	 * @param LocalOffsetCm 相对于组件的局部空间偏移（cm）
	 * @return Pin 约束句柄
	 */
	FEdenPinConstraintHandle RegisterPinConstraintToComponent(
		uint32 ParticleGlobalIndex,
		UPrimitiveComponent* Component,
		const FVector& LocalOffsetCm
	);

	/**
	 * 注册 Pin 约束（固定到世界空间位置 + 旋转约束，用于 Rod）
	 */
	FEdenPinConstraintHandle RegisterPinConstraintWithOrientation(
		uint32 ParticleGlobalIndex,
		const FVector& PositionCm,
		int32 SegmentOrientationIndex,
		const FQuat& TargetOrientation,
		const FQuat& RestDarboux
	);

	/**
	 * 更新 Pin 约束位置（仅当连接到世界空间时有效）
	 */
	void UpdatePinConstraintPosition(
		const FEdenPinConstraintHandle& Handle,
		const FVector& NewPositionCm
	);

	/**
	 * 更新 Pin 约束目标旋转（仅当旋转约束启用时有效）
	 */
	void UpdatePinConstraintOrientation(
		const FEdenPinConstraintHandle& Handle,
		const FQuat& NewTargetOrientation
	);

	/**
	 * 获取粒子位置（用于组件同步，单位：m）
	 */
	const TArray<FVector>& GetParticlePositions() const;

	/**
	 * 获取粒子速度（用于组件同步，单位：m/s）
	 */
	const TArray<FVector>& GetParticleVelocities() const;

	/**
	 * 设置求解器迭代次数
	 */
	void SetSolverIterations(uint32 Iterations);

	/** 设置指定 Handle 中所有粒子的质量（更新逆质量） */
	void SetRopeAllParticleMass(const FEdenRopeHandle& Handle, float NewMass);

	/** 设置指定 Handle 中单个粒子的质量（更新逆质量） */
	void SetSingleParticleMass(const FEdenRopeHandle& Handle, int32 LocalIndex, float NewMass);

	/** 获取指定 Handle 中单个粒子的质量 */
	float GetSingleParticleMass(const FEdenRopeHandle& Handle, int32 LocalIndex) const;

	/** 设置指定 Handle 中所有段的转动惯量（更新逆转动惯量） */
	void SetRodAllSegmentRotationalMass(const FEdenRopeHandle& Handle, float InertiaPerUnitLength, float LengthCm);

#if WITH_EDITOR
	/**
	 * 编辑器侧通知：Component 已完成 RegisterRope/RegisterRod，上报 Component Display Name 到 Evolution 的 Debug Registry
	 */
	void EditorNotifyRopeRegistered(const FEdenRopeHandle& Handle, const FString& InDisplayName);

	/**
	 * 获取 Evolution 的 Debug Registry（可能为 nullptr：Evolution 未创建时）
	 */
	class FEdenRopeDebugRegistry* GetEvolutionDebugRegistry() const;
#endif

	/** 获取渲染插值系数 [0, 1)，0 = 在上一物理步，接近 1 = 接近下一物理步 */
	float GetInterpolationAlpha() const { return InterpolationAlpha; }

	/** 是否启用了固定时间步模式 */
	bool IsFixedTimestepEnabled() const { return SolverConfig.bUseFixedTimestep; }

	/** 获取上一物理步的粒子位置（单位：m），用于渲染插值 */
	const TArray<FVector>& GetPreviousParticlePositions() const;

	/** 获取段方向（世界空间四元数，Rod handle 专用） */
	const TArray<FQuat>& GetSegmentOrientations() const;

	/** 获取上一物理步的段方向（用于渲染插值） */
	const TArray<FQuat>& GetPreviousSegmentOrientations() const;

	// ===== 模拟控制 =====

	/** 控制是否在 Tick 中自动执行物理模拟。关闭后只执行 DebugDraw */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EdenRope")
	bool bAutoSimulate = true;

	/** 执行一步物理模拟（供外部调用，DeltaTime 由调用方维护） */
	UFUNCTION(BlueprintCallable, Category = "EdenRope")
	void SimulateStep(float DeltaTime);

	/**
	 * Debug 步进：执行一步物理模拟
	 * @param DeltaTime 步进时间（秒），由调用方维护
	 */
	UFUNCTION(BlueprintCallable, Category = "EdenRope|Debug")
	void DebugSimulateOneStep(float DeltaTime);

protected:
	/** 将 SolverConfig 中的参数推送给 Evolution 实例 */
	void ApplyConfig();

	// Evolution 实例（XPBD）
	TUniquePtr<IEdenRopeEvolution> Evolution;

	/** 运行时求解器配置（BeginPlay 时从 UEdenRopeSettings 读取） */
	FEdenRopeSolverConfiguration SolverConfig;

	// Fixed Timestep 运行时状态
	float AccumulatedTime = 0.0f;       // 累积的模拟时间余量
	float InterpolationAlpha = 0.0f;    // 渲染插值系数 [0, 1)

	// 空数组用于安全返回
	static const TArray<FVector> EmptyVectorArray;
	static const TArray<FQuat>   EmptyQuatArray;


	/**
	 * 执行 DebugDraw
	 */
	void DoDebugDraw();
};
