// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenRopeComponentBase.h"
#include "EdenRodComponent.generated.h"

/**
 * 位置+方向杆组件（StretchShear + BendTwist 约束，POBCR 模型）
 * 每段有独立的方向四元数，支持弯曲/扭转刚度配置
 *
 * 拓扑约定：
 *   N 个粒子 p[0]..p[N-1]
 *   N-1 段方向 q[0]..q[N-2]，q[k] 位于 p[k] 与 p[k+1] 之间
 *   N-1 个 StretchShear: (p[k], p[k+1], q[k])
 *   N-2 个 BendTwist:    (q[k], q[k+1])
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), hidecategories=(Object, Physics, Collision), editinlinenew, ShowCategories=(Mobility))
class EDENROPE_API UEdenRodComponent : public UEdenRopeComponentBase
{
	GENERATED_BODY()

public:
	UEdenRodComponent();

	//~ Begin IEdenRopeInterface
	virtual bool UsesOrientedParticles() const override { return true; }
	//~ End IEdenRopeInterface

	/** 运行时修改每单位长度段转动惯量（同步更新到 Solver 中的逆转动惯量） */
	UFUNCTION(BlueprintCallable, Category = "Eden Rope")
	void SetSegmentInertiaPerUnitLength(float NewInertia);

protected:
	//~ Begin UEdenRopeComponentBase
	virtual void RegisterToSolver()     override;
	virtual void UnregisterFromSolver() override;
	virtual void SyncFromSolver()       override;
	//~ End UEdenRopeComponentBase

private:
	// ========== StretchShear 约束参数 ==========

	/** StretchShear Constraint: 拉伸 compliance（0 = 刚性） */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|Constraints", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float StretchCompliance = 0.0f;

	/** StretchShear Constraint: 剪切 compliance（材质 X 轴方向） */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|Constraints", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float Shear1Compliance = 0.0f;

	/** StretchShear Constraint: 剪切 compliance（材质 Y 轴方向） */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|Constraints", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float Shear2Compliance = 0.0f;

	// ========== BendTwist 约束参数 ==========

	/** BendTwist Constraint: 扭转 compliance */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|Constraints", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float TwistCompliance = 0.0f;

	/** BendTwist Constraint: 弯曲 compliance（材质 X 轴弯曲） */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|Constraints", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float Bend1Compliance = 0.0f;

	/** BendTwist Constraint: 弯曲 compliance（材质 Y 轴弯曲） */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|Constraints", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float Bend2Compliance = 0.0f;

	/** 每单位长度段转动惯量。值越大，杆对弯曲/扭转的抵抗越强, unit: [M]*[L] */
	UPROPERTY(EditAnywhere, Category = "Eden Rope", meta = (AllowPrivateAccess = "true", ClampMin = "0.001"))
	float SegmentInertiaPerUnitLength = 1.0f;

	/** 保持初始形状：true = 从 Spline 曲线计算初始 Darboux 向量（杆会维持弯曲形状），
	 *  false = RestDarboux 为零（平衡态为直线） */
	UPROPERTY(EditAnywhere, Category = "Eden Rope", meta = (AllowPrivateAccess = "true"))
	bool bKeepInitialShape = true;
};
