// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EdenRopeSolverConfiguration.h"
#include "EdenRopeSettings.generated.h"

class AEdenRopeSolverActor;

/**
 * EdenRope 插件项目级设置
 * 在 Project Settings > Plugins > EdenRope 中显示
 */
UCLASS(config = EdenRope, defaultconfig, meta = (DisplayName = "EdenRope"))
class EDENROPE_API UEdenRopeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEdenRopeSettings();

	/** 求解器配置 */
	UPROPERTY(config, EditAnywhere, Category = "Solver", meta = (ShowOnlyInnerProperties))
	FEdenRopeSolverConfiguration SolverConfiguration;

	/** Solver Actor 子类（可设置蓝图子类来自定义调试逻辑） */
	UPROPERTY(config, EditAnywhere, Category = "Solver", meta = (MetaClass = "/Script/EdenRope.EdenRopeSolverActor"))
	TSoftClassPtr<AEdenRopeSolverActor> SolverActorClass;

	/**
	 * EdenParticle 碰撞通道名称
	 * 需要在 Project Settings > Collision > Object Channels 中创建对应的自定义通道
	 * 如果找不到此通道，将默认使用 PhysicsBody 通道
	 */
	UPROPERTY(config, EditAnywhere, Category = "Collision", meta = (DisplayName = "EdenParticle Channel Name"))
	FName EdenParticleChannelName = FName("EdenParticle");

	/** 获取单例设置实例 (CDO) */
	static const UEdenRopeSettings* Get();

	// UDeveloperSettings overrides
	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("EdenRope"); }
};
