// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EdenRopeComponentBase.h"
#include "EdenRopeComponent.generated.h"

/**
 * 纯位置绳索组件（Distance + Bend 约束）
 * 继承 UEdenRopeComponentBase，实现 Rope 专属注册/同步逻辑
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), hidecategories=(Object, Physics, Collision), editinlinenew, ShowCategories=(Mobility))
class EDENROPE_API UEdenRopeComponent : public UEdenRopeComponentBase
{
	GENERATED_BODY()

public:
	UEdenRopeComponent();

	//~ Begin IEdenRopeInterface
	virtual bool UsesOrientedParticles() const override { return false; }
	//~ End IEdenRopeInterface

protected:
	//~ Begin UEdenRopeComponentBase
	virtual void RegisterToSolver()     override;
	virtual void UnregisterFromSolver() override;
	virtual void SyncFromSolver()       override;
	//~ End UEdenRopeComponentBase

private:
	// ========== Rope 专属属性 ==========

	UPROPERTY(EditAnywhere, Category = "Eden Rope|Constraints", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float DistanceStiffness = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Eden Rope|Constraints", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	float DistanceCompressThreshold = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Eden Rope|Constraints", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float BendStiffness = 0.0f;

	/** 最小弯曲角度系数, 如果连续三个点形成的夹角过小, 说明弯曲程度太大了, 需要施加弯曲力, 0~1 数值越小, 越容易开始施加弯曲力 */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|Constraints", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float MinBendAngleCoefficient = 0.1f;
};
