// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Particle/ObjectState.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/ShapeInstance.h"
#include "Engine/EngineTypes.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "EdenRopeCollisionChannel.h"

class UPrimitiveComponent;

namespace Eden
{

/**
 * 碰撞候选结果（从 BVH 宽相位查询中获取）
 * 存储 Chaos 粒子信息和变换，用于后续窄相位检测
 */
struct FCollisionCandidate
{
	Chaos::FGeometryParticle* Particle = nullptr;
	Chaos::EObjectStateType ObjectState = Chaos::EObjectStateType::Uninitialized;
	FTransform WorldTransform;
	
	// 缓存的几何信息（避免重复获取）
	Chaos::FImplicitObjectRef Geometry;
	
	// 关联的 PrimitiveComponent（用于 Dynamic 刚体的反作用力）
	TWeakObjectPtr<UPrimitiveComponent> OwnerComponent;
	
	FCollisionCandidate() = default;
	
	FCollisionCandidate(
		Chaos::FGeometryParticle* InParticle, 
		Chaos::EObjectStateType InState, 
		const FTransform& InTransform, 
		const Chaos::FImplicitObjectRef& InGeometry,
		UPrimitiveComponent* InOwner = nullptr)
		: Particle(InParticle)
		, ObjectState(InState)
		, WorldTransform(InTransform)
		, Geometry(InGeometry)
		, OwnerComponent(InOwner)
	{
	}
	
	bool IsDynamic() const { return ObjectState == Chaos::EObjectStateType::Dynamic; }
	bool IsStatic() const { return ObjectState == Chaos::EObjectStateType::Static; }
	bool IsKinematic() const { return ObjectState == Chaos::EObjectStateType::Kinematic; }
};

/**
 * EdenRope 专用的 Overlap Visitor
 * 实现 ISpatialVisitor 接口，用于查询 Chaos BVH 空间加速结构
 * 支持 EdenParticle 碰撞通道过滤
 */
class FEdenRopeOverlapVisitor : public Chaos::ISpatialVisitor<Chaos::FAccelerationStructureHandle, Chaos::FReal>
{
public:
	using FVisitorData = Chaos::TSpatialVisitorData<Chaos::FAccelerationStructureHandle>;
	
	// 查询结果：碰撞候选列表
	TArray<FCollisionCandidate> Candidates;
	
	// EdenParticle 碰撞通道（从 UEdenRopeSettings 动态查找，默认 ECC_PhysicsBody）
	ECollisionChannel EdenParticleChannel = GEdenParticleCollisionChannel;
	
	// 是否启用碰撞通道过滤
	bool bEnableChannelFilter = true;
	
	FEdenRopeOverlapVisitor() = default;
	
	explicit FEdenRopeOverlapVisitor(ECollisionChannel InChannel, bool bInEnableFilter = true)
		: EdenParticleChannel(InChannel)
		, bEnableChannelFilter(bInEnableFilter)
	{
	}
	
	// ISpatialVisitor 接口实现
	virtual bool Overlap(const FVisitorData& Instance) override;
	
	virtual bool Raycast(const FVisitorData& Instance, Chaos::FQueryFastData& CurData) override
	{
		return true;
	}
	
	virtual bool Sweep(const FVisitorData& Instance, Chaos::FQueryFastData& CurData) override
	{
		return true;
	}
	
	// NOTE (UE5.8): Chaos::ISpatialVisitor::GetQueryData()/GetSimData() are now
	// `final` (and deprecated 5.8 -> "Implement PrePreFilter instead"). They can no
	// longer be overridden. The base implementations already return nullptr, which
	// matches the previous behavior here, so the overrides are simply removed.
	virtual const void* GetQueryPayload() const override { return nullptr; }
	virtual bool HasBlockingHit() const override { return false; }
	
	void Reset()
	{
		Candidates.Reset();
	}
	
private:
	/**
	 * 检查粒子的 Shape 是否与 EdenParticle 通道碰撞
	 * @param Particle Chaos 几何粒子
	 * @return 如果任何 Shape 与 EdenParticle 通道 Block，返回 true
	 */
	bool ShouldCollideWithEdenParticle(Chaos::FGeometryParticle* Particle) const;
};

} // namespace Eden
