// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "SceneInterface.h"
#include "Components/MeshComponent.h"
#include "Interface/EdenRopeInterface.h"
#include "Materials/MaterialRelevance.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "Misc/EngineVersionComparison.h"
#include "RigidRope/EdenRigidRopeAsset.h"
#include "EdenRigidRopeComponent.generated.h"

class FPrimitiveSceneProxy;
class UBodySetup;
class UEdenRigidRopeComponent;
class UInstancedStaticMeshComponent;

/**
 * Tick function that does post physics work on rope component.
 * This executes in EndPhysics (after physics simulation is done)
 */
USTRUCT()
struct FEdenRigidRopeEndPhysicsTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	/** Target rope component */
	UEdenRigidRopeComponent* Target;

	virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template<>
struct TStructOpsTypeTraits<FEdenRigidRopeEndPhysicsTickFunction> : public TStructOpsTypeTraitsBase2<FEdenRigidRopeEndPhysicsTickFunction>
{
	enum
	{
		WithCopy = false
	};
};

/** Struct containing information about a point along the rope */
struct FEdenRigidRopeParticle
{
	FEdenRigidRopeParticle()
	: bFree(true)
	, Position(0,0,0)
	, OldPosition(0,0,0)
	{}

	/** If this point is free (simulating) or fixed to something */
	bool bFree;
	/** Current position of point */
	FVector Position;
	/** Position of point on previous iteration */
	FVector OldPosition;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class EDENROPE_API UEdenRigidRopeComponent : public UMeshComponent, public IEdenRopeInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UEdenRigidRopeComponent();

	//~ Begin UObject Interface.
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface.

	//~ Begin UActorComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void SendRenderDynamicData_Concurrent() override;
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;
	//~ End UActorComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const override;
	virtual bool HasAnySockets() const override;
	virtual bool DoesSocketExist(FName InSocketName) const override;
	virtual FTransform GetSocketTransform(FName InSocketName, ERelativeTransformSpace TransformSpace = RTS_World) const override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;
	//~ End USceneComponent Interface.

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual void RegisterComponentTickFunctions(bool bRegister) override;

	// --- 物理状态生命周期 ---
	virtual class UBodySetup* GetBodySetup() override;
	virtual FBodyInstance* GetBodyInstance(FName BoneName = NAME_None, bool bGetWelded = true, int32 Index = INDEX_NONE) const override;
	virtual bool CanEditSimulatePhysics() override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;

	// --- 多 Body：物理模拟控制与状态查询 ---
	virtual void SetSimulatePhysics(bool bSimulate) override;
	virtual bool IsSimulatingPhysics(FName BoneName = NAME_None) const override;
	virtual bool IsAnySimulatingPhysics() const override;

	// --- 多 Body：唤醒/休眠 ---
	virtual void WakeAllRigidBodies() override;
	virtual void PutAllRigidBodiesToSleep() override;
	virtual bool IsAnyRigidBodyAwake() override;

	// --- 多 Body：速度/位置/旋转 ---
	virtual void SetAllPhysicsLinearVelocity(FVector NewVel, bool bAddToCurrent = false) override;
	virtual void SetAllPhysicsAngularVelocityInRadians(const FVector& NewAngVel, bool bAddToCurrent = false) override;
	virtual void SetAllPhysicsPosition(FVector NewPos) override;
	virtual void SetAllPhysicsRotation(FRotator NewRot) override;
	virtual void SetAllPhysicsRotation(const FQuat& NewRot) override;

	// --- 多 Body：力/冲量 ---
	virtual void AddRadialImpulse(FVector Origin, float Radius, float Strength, enum ERadialImpulseFalloff Falloff, bool bVelChange = false) override;
	virtual void AddRadialForce(FVector Origin, float Radius, float Strength, enum ERadialImpulseFalloff Falloff, bool bAccelChange = false) override;

	// --- 多 Body：质量/材质/CCD ---
	virtual float GetMass() const override;
	virtual void SetAllMassScale(float InMassScale = 1.f) override;
	virtual void SetAllUseCCD(bool InUseCCD) override;
	virtual void SetPhysMaterialOverride(class UPhysicalMaterial* NewPhysMaterial) override;

	// --- 多 Body：碰撞设置 ---
	virtual void SetNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision) override;
	virtual void OnComponentCollisionSettingsChanged(bool bUpdateOverlaps = true) override;
	virtual void UpdatePhysicsToRBChannels() override;
#if WITH_EDITOR
	virtual void UpdateCollisionProfile() override;
#endif

	// --- 多 Body：碰撞查询 ---
	virtual bool LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params) override;
	virtual bool SweepComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FQuat& ShapeWorldRotation, const FCollisionShape& CollisionShape, bool bTraceComplex = false) override;
	virtual bool OverlapComponent(const FVector& Pos, const FQuat& Rot, const FCollisionShape& CollisionShape) const override;
	virtual bool GetSquaredDistanceToCollision(const FVector& Point, float& OutSquaredDistance, FVector& OutClosestPointOnCollision) const override;

	// --- 物理同步：Component ↔ RootBody 变换还原 ---
	virtual FTransform GetComponentTransformFromBodyInstance(FBodyInstance* UseBI) override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin IPhysicsComponent Interface.
	virtual Chaos::FPhysicsObject* GetPhysicsObjectById(Chaos::FPhysicsObjectId Id) const override;
	virtual Chaos::FPhysicsObject* GetPhysicsObjectByName(const FName& Name) const override;
	virtual TArray<Chaos::FPhysicsObject*> GetAllPhysicsObjects() const override;
	//~ End IPhysicsComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual int32 GetNumMaterials() const override;
	//~ End UMeshComponent Interface.

	//~ Begin IEdenRopeInterface
	virtual int32 GetRopeSegmentNum() const override { return RopeAsset ? RopeAsset->NumSegments : 0; }
	virtual float GetRopeCableWidth() const override { return RopeAsset ? RopeAsset->CableWidth : 0.f; }
	virtual int32 GetRopeNumSides() const override { return RopeAsset ? RopeAsset->NumSides : 4; }
	virtual float GetRopeTileMaterial() const override { return RopeAsset ? RopeAsset->TileMaterial : 1.f; }
	virtual UMaterialInterface* GetRopeMaterial() const override { return GetMaterial(0); }
#if UE_VERSION_NEWER_THAN(5, 7, 0)
	virtual FMaterialRelevance GetRopeMaterialRelevance() const override { return GetMaterialRelevance(GetScene()->GetShaderPlatform()); }
#else
	virtual FMaterialRelevance GetRopeMaterialRelevance() const override { return GetMaterialRelevance(GetScene()->GetFeatureLevel()); }
#endif
	virtual uint8 GetRopeSmoothing() const override { return RopeAsset ? RopeAsset->Smoothing : 0; }
	virtual float GetRopeDecimation() const override { return RopeAsset ? RopeAsset->Decimation : 0.f; }
	virtual void GetRopeParticleLocations(TArray<FVector>& OutLocations) const override;
	virtual bool NeedRigidDebugRender() const override { return true; }
	virtual UBodySetup* GetRopeBodySetup() const override { return RopeBodySetup; }
	virtual void GetRopeBodyTransforms(TArray<FTransform>& OutTransforms) const override;
	virtual FVector GetRopeStartWorldLocation() const override;
	virtual FVector GetRopeEndWorldLocation() const override;
	//~ End IEdenRopeInterface

	/** Get the physics bodies array (for SceneProxy collision visualization) */
	const TArray<FBodyInstance*>& GetBodies() const { return Bodies; }

	/**
	 * 获取所有 Particle 相对于 Component Space 的变换缓存。
	 * 数组大小为 NumParticles（NumSegments + 1），与 BodyInstance 空间一致。
	 * ParticleSpaceTransforms[i] 在世界空间下等价于 Bodies[i]->GetUnrealWorldTransform()（对于 i < NumSegments）。
	 */
	const TArray<FTransform>& GetParticleSpaceTransforms() const { return ParticleSpaceTransforms; }

	/** 
	 * Get the physics body associated with a socket name.
	 * For socket "Particle_i", returns the body of the segment starting at that particle.
	 * Note: The last particle (Particle_N where N = NumSegments) has no associated body.
	 * @param SocketName - Socket name in format "Particle_0", "Particle_1", etc.
	 * @return The body instance, or nullptr if not found or invalid
	 */
	FBodyInstance* GetBodyBySocketName(FName SocketName) const;

	/**
	 * Get the number of physics bodies in this rope.
	 * This equals NumSegments (one body per segment).
	 */
	UFUNCTION(BlueprintCallable, Category = "Rope|Physics")
	int32 GetNumBodies() const { return Bodies.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Rope|Physics")
	void SetAllLinearDamping(float InDamping);

	UFUNCTION(BlueprintCallable, Category = "Rope|Physics")
	void SetAllAngularDamping(float InDamping);

	/**
	 * Get a physics body by its index.
	 * @param Index - Body index (0 to NumSegments-1)
	 * @return The body instance, or nullptr if index is invalid
	 */
	FBodyInstance* GetBodyByIndex(int32 Index) const;

	// ========== Asset Reference ==========

	/** Local axis direction the rope extends along when initialized */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope")
	EEdenRopeForwardAxis ForwardAxis = EEdenRopeForwardAxis::PositiveX;

	/** Get a unit vector in local space for the chosen ForwardAxis */
	FVector GetLocalForwardVector() const;

	/** The RigidRope asset that defines this rope's parameters (segments, physics, rendering, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope")
	TObjectPtr<UEdenRigidRopeAsset> RopeAsset;

	/** 当前第一个 Particle 的 Socket 名称（只读，由 RopeAsset 自动生成） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rope")
	FName FirstParticleSocketName;

	/** Set a new RopeAsset at runtime. Destroys current physics state and re-initializes from the new asset. */
	UFUNCTION(BlueprintCallable, Category="Rope")
	void SetRopeAsset(UEdenRigidRopeAsset* NewAsset);

	// ========== Rope Rendering Properties ==========
	// 注意：NumSegments, CableWidth, NumSides, TileMaterial, Smoothing, Decimation, RopeLength
	// 已迁移至 UEdenRigidRopeAsset，通过 RopeAsset 引用读取

	// ========== Rope Physics Properties ==========
	// 注意：SegmentMass, SwingLimitAngle, SwingStiffness 已迁移至 UEdenRigidRopeAsset
	// 物理模拟开关直接使用 UPrimitiveComponent::BodyInstance.bSimulatePhysics（面板上的 "Simulate Physics"）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope")
	bool bDebugUseConstraints = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope")
	float DebugPhysicsThickness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope")
	FRotator DebugPhysicsDeltaRotation;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

private:
	/**
	 * Parse a bone/socket name and extract the particle index.
	 * Recognizes both the Asset's BoneNamePrefix (e.g. "StringBone_3")
	 * and the legacy "Particle_" prefix for backward compatibility.
	 * @return Particle index [0..NumParticles-1], or INDEX_NONE if not matched.
	 */
	int32 ParseBoneIndex(FName InName) const;

	// ========== Segment Mesh Rendering ==========

	/** Internal ISMC used for Segment Mesh rendering (not serialized). Created on demand when RopeAsset->RenderMode == SegmentMesh. */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> SegmentMeshISMC = nullptr;

	/** Whether the current RopeAsset configuration should use SegmentMesh rendering path. */
	bool ShouldUseSegmentMeshRender() const;

	/** Create the ISMC and pre-fill NumSegments instances, then run UpdateSegmentMeshInstances(). */
	void CreateSegmentMeshISMC();

	/** Destroy the ISMC if it exists. Safe to call multiple times (idempotent). */
	void DestroySegmentMeshISMC();

	/** Update every ISMC instance transform from the current Particles array. */
	void UpdateSegmentMeshInstances();

	/** Reference length along the SegmentMesh forward axis (BoxExtent * 2). Returns 1.0 if mesh is invalid or degenerate. */
	float GetSegmentMeshRefLength() const;

	/** 0/1/2 for X/Y/Z, based on RopeAsset->SegmentMeshForwardAxis. */
	int32 GetSegmentMeshForwardAxisIndex() const;

	/** Unit vector for the SegmentMesh forward axis. */
	FVector GetSegmentMeshForwardAxisVector() const;

	/** Get start and end position for the rope */
	void GetEndPositions(FVector& OutStartPosition, FVector& OutEndPosition) const;

	// ========== Physics Internal Functions ==========

	/** Create the shared BodySetup for rope segments */
	void CreateRopeBodySetup();

	/** Initialize all physics bodies */
	void InitRopeBodies(FPhysScene* PhysScene);

	/** Initialize constraints between bodies */
	void InitRopeConstraints();

	/** Terminate all physics bodies and constraints */
	void TermRopePhysics();

	/**
	 * After bodies are created, scan the owning Actor for external
	 * UPhysicsConstraintComponents that reference this component.
	 * If any failed to init (because our bodies didn't exist yet),
	 * re-trigger their InitComponentConstraint().
	 */
	void ReinitPendingExternalConstraints();

	/**
	 * Before destroying bodies, terminate any external
	 * UPhysicsConstraintComponents that reference this component
	 * to prevent dangling FBodyInstance pointers.
	 */
	void NotifyExternalConstraintsAboutDestruction();

	/** Sync particle positions from physics bodies */
	void SyncParticlesFromBodies();

	/**
	 * Teleport all physics bodies to match current Particle positions.
	 * Called from OnUpdateTransform after particles have been offset.
	 * @param Teleport - Teleport type to pass to FBodyInstance::SetBodyTransform
	 */
	void TeleportAllBodies(ETeleportType Teleport);

	/**
	 * Check if any physics body is actually simulating physics at runtime.
	 * Uses per-body IsInstanceSimulatingPhysics() instead of the config value BodyInstance.bSimulatePhysics.
	 * In editor (without Simulate), bodies are kinematic even if bSimulatePhysics is true.
	 * @return true if at least one body is actually simulating physics
	 */
	bool IsAnyBodySimulatingPhysics() const;

	/** Update the registration state of the end physics tick function */
	void UpdateEndPhysicsTickRegisteredState();

	/** Register/Unregister the end physics tick function */
	void RegisterEndPhysicsTick(bool bRegister);

	/** Check if we should run the end physics tick */
	bool ShouldRunEndPhysicsTick() const;

	/** Called after physics simulation completes */
	void EndPhysicsTickComponent(FEdenRigidRopeEndPhysicsTickFunction& ThisTickFunction);

	/**
	 * 统一更新 ParticleSpaceTransforms 数组。
	 * 根据当前 Particles 数组的世界位置和 GetComponentTransform() 计算所有 Component Space 变换。
	 * 在所有 Particle 位置更新路径（物理同步、Tick、OnUpdateTransform、Teleport）中调用。
	 */
	void UpdateParticleSpaceTransforms();

	// ========== Runtime Data ==========

	/** Array of rope particles */
	TArray<FEdenRigidRopeParticle> Particles;

	/** Shared BodySetup for all segments (Transient - not saved) */
	UPROPERTY(Transient)
	TObjectPtr<UBodySetup> RopeBodySetup;

	/** Physics bodies for each segment */
	TArray<FBodyInstance*> Bodies;

	/** Constraints connecting adjacent bodies */
	TArray<FConstraintInstance*> Constraints;

	/** Physics aggregate to group all bodies (prevents self-collision) */
	FPhysicsAggregateHandle Aggregate;

	/** Tick function for post-physics sync */
	FEdenRigidRopeEndPhysicsTickFunction EndPhysicsTickFunction;

	/**
	 * 每个 Particle 相对于 Component Space 的变换缓存（Particle Space == Body Space）。
	 * 数组大小为 NumParticles（NumSegments + 1）。
	 * 对于 i < NumSegments：ParticleSpaceTransforms[i] 与 Bodies[i] 的世界变换相对于 Component 变换一致。
	 * 对于 i == NumSegments（最后一个 Particle）：由最后一个 Body 沿 Forward 方向偏移 SegmentLength 推导。
	 */
	TArray<FTransform> ParticleSpaceTransforms;

	/**
	 * Component 相对于 RootBody (Bodies[0]) 的固定偏移变换。
	 * 语义：TransformToRoot = ComponentTransform * RootBodyTransform.Inverse()
	 * 用于 GetComponentTransformFromBodyInstance 中还原 Component 变换：
	 *   NewComponentTransform = TransformToRoot * RootBody_CurrentWorldTransform
	 * 
	 * 仅在以下时机更新，物理模拟期间保持不变以避免循环依赖：
	 * - 物理体初始化时（OnCreatePhysicsState）
	 * - 组件注册时（OnRegister，从 Particle 数据推导）
	 * - 外部 Teleport 时（OnUpdateTransform，非 SkipPhysicsUpdate）
	 */
	FTransform TransformToRoot;

#if WITH_EDITOR
	/** 监听 RopeAsset 属性变更，自动同步更新组件 */
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
	FDelegateHandle AssetPropertyChangedHandle;
#endif

	friend class FEdenRopeSceneProxy;
	friend struct FEdenRigidRopeEndPhysicsTickFunction;
};
