// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneInterface.h"
#include "Misc/EngineVersionComparison.h"
#include "Components/MeshComponent.h"
#include "Components/SplineComponent.h"
#include "Render/EdenRopeRenderTypes.h"
#include "Handles/EdenRopeHandle.h"
#include "Materials/MaterialRelevance.h"
#include "Interface/EdenRopeInterface.h"
#include "EdenRopeComponentBase.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnRopeRegistered, const FEdenRopeHandle&);

class AEdenRopeSolverActor;
class UInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * 抽象基类：共享渲染、注册、同步逻辑
 * 子类实现 RegisterToSolver / UnregisterFromSolver / SyncFromSolver
 */
UCLASS(Abstract, hidecategories=(Object, Physics, Collision), ShowCategories=(Mobility))
class EDENROPE_API UEdenRopeComponentBase : public UMeshComponent, public IEdenRopeInterface
{
	GENERATED_BODY()

public:
	UEdenRopeComponentBase();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnUnregister() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	//~ Begin UActorComponent Interface.
	virtual void OnRegister() override;
	virtual void SendRenderDynamicData_Concurrent() override;
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual bool ShouldCreatePhysicsState() const override { return false; }
	//~ End UActorComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End USceneComponent Interface.

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual UBodySetup* GetBodySetup() override { return nullptr; }
	//~ End UPrimitiveComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual int32 GetNumMaterials() const override;
	//~ End UMeshComponent Interface.

	//~ Begin IEdenRopeInterface
	virtual int32 GetRopeSegmentNum() const override { return NumParticles - 1; }
	virtual float GetRopeCableWidth() const override { return CableWidth; }
	virtual int32 GetRopeNumSides() const override { return NumSides; }
	virtual float GetRopeTileMaterial() const override { return TileMaterial; }
	virtual uint8 GetRopeSmoothing() const override { return Smoothing; }
	virtual float GetRopeDecimation() const override { return Decimation; }
	virtual UMaterialInterface* GetRopeMaterial() const override { return GetMaterial(0); }
#if UE_VERSION_NEWER_THAN(5, 7, 0)
	virtual FMaterialRelevance GetRopeMaterialRelevance() const override { return GetMaterialRelevance(GetScene()->GetShaderPlatform()); }
#else
	virtual FMaterialRelevance GetRopeMaterialRelevance() const override { return GetMaterialRelevance(GetScene()->GetFeatureLevel()); }
#endif
	virtual void GetRopeParticleLocations(TArray<FVector>& OutLocations) const override;
	// UsesOrientedParticles() 由子类覆盖
	virtual FVector GetRopeStartWorldLocation() const override;
	virtual FVector GetRopeEndWorldLocation() const override;
	//~ End IEdenRopeInterface

	/** 获取是否已注册到 Solver */
	bool IsSolverRegistered() const { return bSolverRegistered; }

	/** 运行时修改每粒子质量（同步更新到 Solver 中的逆质量） */
	UFUNCTION(BlueprintCallable, Category = "Eden Rope")
	void SetMassPerParticle(float NewMass);

	/**
	 * 运行时设置单个粒子的质量
	 * @param LocalIndex 粒子在绳索中的局部索引（0 ~ NumParticles-1）
	 * @param NewMass 新的质量（kg），必须 > 0。不会修改固定粒子（InvMass == 0）的质量
	 */
	UFUNCTION(BlueprintCallable, Category = "Eden Rope")
	void SetSingleParticleMass(int32 LocalIndex, float NewMass);

	/**
	 * 获取单个粒子的质量
	 * @param LocalIndex 粒子在绳索中的局部索引（0 ~ NumParticles-1）
	 * @return 质量（kg），固定粒子返回 0，无效索引返回 -1
	 */
	UFUNCTION(BlueprintCallable, Category = "Eden Rope")
	float GetSingleParticleMass(int32 LocalIndex) const;

	/** 获取绳索的 Handle */
	const FEdenRopeHandle& GetRopeHandle() const { return RopeHandle; }

	FOnRopeRegistered OnRopeRegistered;

	// ========== Spline Authoring ==========

	/** Persistent spline curve data used to author the initial rope/rod shape.
	 *  Serialized with the component, usable in any Actor/Blueprint. 
	 *  Space: [Component's Object Space]
	 */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|Spline Authoring")
	FSplineCurves AuthoringCurves;

#if WITH_EDITORONLY_DATA
	/** Placeholder for Edit Spline button (replaced by DetailCustomization in EdenRopeEditor module) */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Eden Rope|Spline Authoring")
	bool bEditSplineButton = false;
#endif

	// ========== Rope Rendering Properties ==========

	/** Rendering mode: Tube (procedural cylinder) or Segment Mesh (instanced static mesh per segment) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eden Rope|Rendering")
	ERopeRenderMode RenderMode = ERopeRenderMode::Tube;

	/** How wide the rope geometry is */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eden Rope|Rendering", meta=(ClampMin = "0.01", UIMin = "0.01", UIMax = "50.0", EditCondition="RenderMode==ERopeRenderMode::Tube"))
	float CableWidth = 5.0f;

	/** Number of sides of the rope geometry */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Eden Rope|Rendering", meta=(ClampMin = "1", UIMax = "16", EditCondition="RenderMode==ERopeRenderMode::Tube"))
	int32 NumSides = 4;

	/** How many times to repeat the material per rope segment (1.0 = one UV tile per segment) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eden Rope|Rendering", meta=(UIMin = "0.1", UIMax = "8", EditCondition="RenderMode==ERopeRenderMode::Tube"))
	float TileMaterial = 1.0f;

	/** Chaikin smoothing iterations applied to the render path (0 = none, 1-3 = subdivide).
	 *  Each level doubles the number of render points per segment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Eden Rope|Rendering", meta=(ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3", EditCondition="RenderMode==ERopeRenderMode::Tube"))
	uint8 Smoothing = 0;

	/** Decimation curvature threshold (0 = no decimation). Straight sections are simplified before smoothing,
	 *  so that bends get more render detail while straight parts stay lightweight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Eden Rope|Rendering", meta=(ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition="RenderMode==ERopeRenderMode::Tube"))
	float Decimation = 0.0f;

	/** Static mesh placed at each rope segment (Segment Mesh mode only) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eden Rope|Rendering",
		meta=(EditCondition="RenderMode==ERopeRenderMode::SegmentMesh"))
	UStaticMesh* SegmentMesh = nullptr;

	/** Which local axis of the mesh is aligned with the segment direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eden Rope|Rendering",
		meta=(EditCondition="RenderMode==ERopeRenderMode::SegmentMesh"))
	ESegmentMeshForwardAxis SegmentMeshForwardAxis = ESegmentMeshForwardAxis::X;

	/** Scale applied to the two cross-section axes (the forward axis is auto-scaled to fill the segment) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eden Rope|Rendering",
		meta=(EditCondition="RenderMode==ERopeRenderMode::SegmentMesh"))
	FVector2D SegmentMeshCrossScale = FVector2D(1.0f, 1.0f);

	/** Twist angle (degrees) applied to every other segment around the forward axis.
	 *  0 = no alternation. 90 = chain-link style (even: 0°, odd: 90°). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eden Rope|Rendering",
		meta=(EditCondition="RenderMode==ERopeRenderMode::SegmentMesh"))
	float SegmentMeshTwistAngle = 0.0f;

	/** Forward-axis scale multiplier applied on top of the auto-fit scale.
	 *  1.0 = exact fit. >1.0 = segments overlap (interlocking effect). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eden Rope|Rendering",
		meta=(EditCondition="RenderMode==ERopeRenderMode::SegmentMesh", ClampMin="0.01"))
	float SegmentMeshForwardScale = 1.0f;

	/** Internal ISMC used for Segment Mesh rendering (not serialized) */
	UPROPERTY()
	UInstancedStaticMeshComponent* SegmentMeshISMC = nullptr;

protected:
	// ========== 共享物理参数 ==========

	UPROPERTY(EditAnywhere, Category = "Eden Rope")
	int32 NumParticles = 10;
	
	// Space: [Component's Object Space], can be scaled
	UPROPERTY(EditAnywhere, Category = "Eden Rope", meta = (Units = "cm"))
	float Length = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Eden Rope")
	float MassPerParticle = 1.0f;

	// Space: [Component's Object Space], can be scaled
	UPROPERTY(EditAnywhere, Category = "Eden Rope")
	float ParticleRadius = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Eden Rope")
	float DampingCoefficient = 0.01f;

	// ========== Per-Component Friction ==========

	/** 地面比例摩擦系数 k_fp (0~1, 1=无摩擦, 0=完全停止) */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|Friction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GroundFrictionProportional = 0.95f;

	/** 地面常值摩擦系数 k_fc (单位: m/s)，用于低速时完全停止 */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|Friction", meta = (ClampMin = "0.0"))
	float GroundFrictionConstant = 0.01f;

	// ========== Per-Component Self Collision ==========

	/** 启用 segment 间碰撞（capsule-capsule 自碰撞 + 跨绳碰撞） */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|SelfCollision")
	bool bEnableSelfCollision = false;

	/** 同一根绳索中跳过的相邻 segment 数（拓扑距离），避免与距离/弯曲约束冲突。1 = 只跳过共享粒子的相邻 segment */
	UPROPERTY(EditAnywhere, Category = "Eden Rope|SelfCollision", meta = (ClampMin = "1", ClampMax = "10", EditCondition = "bEnableSelfCollision"))
	int32 SelfCollisionSkipCount = 3;

	// ========== 子类必须重写的接口（UCLASS 不支持纯虚，用 unimplemented() 代替）==========

	virtual void RegisterToSolver()   { unimplemented(); }
	virtual void UnregisterFromSolver() { unimplemented(); }
	virtual void SyncFromSolver()     { unimplemented(); }

	// ========== 内部方法 ==========

	/** 初始化默认粒子位置（用于 Editor 预览） */
	void InitializeDefaultParticlePositions();

#if WITH_EDITOR
	/** During editor tick, sync SplineCurves from an active editing SplineComponent
	 *  (created by DetailCustomization) back to AuthoringCurves for real-time preview. */
	void SyncFromEditingSpline();
#endif

	// ========== Segment Mesh ISMC helpers ==========

	void CreateSegmentMeshISMC();
	void DestroySegmentMeshISMC();
	void UpdateSegmentMeshInstances();

	float GetSegmentMeshRefLength() const;
	int32 GetForwardAxisIndex() const;
	FVector GetForwardAxisVector() const;

	// ========== Spline Authoring Helpers ==========

	/**
	 * Sample positions (and optionally orientations) directly from FSplineCurves.
	 * Uses FInterpCurve::Eval / EvalDerivative — no USplineComponent needed.
	 * @param Curves        The spline curves to sample from
	 * @param NumPts        Number of sampling points (== NumParticles)
	 * @param LengthCm      Total rope/rod length in cm
	 * @param OutPositions  Output positions (world space, cm)
	 * @param OutOrientations If non-null, filled with per-segment orientations (NumPts-1 entries)
	 * @return true if sampling succeeded
	 */
	bool SampleFromCurves(
		const FSplineCurves& Curves,
		int32 NumPts,
		float LengthCm,
		TArray<FVector>& OutPositions,
		TArray<FQuat>* OutOrientations = nullptr) const;

	// ========== 运行时缓存 ==========

	/** Cached particle positions from Solver (world space, cm) */
	TArray<FVector> CachedParticlePositions;

	/** Cached particle orientations from Solver (world space, only valid when UsesOrientedParticles()) */
	TArray<FQuat> CachedParticleOrientations;

	FEdenRopeHandle RopeHandle;

	UPROPERTY(Transient)
	TWeakObjectPtr<AEdenRopeSolverActor> CachedSolver;

	bool bSolverRegistered = false;
};
