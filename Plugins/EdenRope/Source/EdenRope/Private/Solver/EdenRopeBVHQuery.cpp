// Copyright Eden Games. All Rights Reserved.

#include "EdenRopeBVHQuery.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectType.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Components/PrimitiveComponent.h"
#include "Misc/EngineVersionComparison.h"
#include "PhysicsEngine/BodyInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogEdenRopeBVH, Log, All);

namespace Eden
{

bool FEdenRopeOverlapVisitor::Overlap(const FVisitorData& Instance)
{
	// 获取 GT 粒子
	Chaos::FGeometryParticle* Particle = Instance.Payload.GetExternalGeometryParticle_ExternalThread();
	if (!Particle)
	{
		return true;
	}
	
	// 获取物体状态
	Chaos::EObjectStateType ObjectState = Chaos::EObjectStateType::Static;
	if (const Chaos::FPBDRigidParticle* RigidParticle = Particle->CastToRigidParticle())
	{
		ObjectState = RigidParticle->ObjectState();
	}
	
	// 只处理 Static、Dynamic 和 Kinematic 物体
	if (ObjectState == Chaos::EObjectStateType::Uninitialized || 
	    ObjectState == Chaos::EObjectStateType::Sleeping)
	{
		if (ObjectState == Chaos::EObjectStateType::Uninitialized)
		{
			return true;
		}
		// 对于 Sleeping，当作 Dynamic 处理
		ObjectState = Chaos::EObjectStateType::Dynamic;
	}
	
	// 碰撞通道过滤
	if (bEnableChannelFilter && !ShouldCollideWithEdenParticle(Particle))
	{
		return true;
	}
	
	// 获取几何信息
	Chaos::FImplicitObjectRef Geometry = Particle->GetGeometry();
	if (!Geometry)
	{
		return true;
	}
	
	// 构建世界变换
	const FVector Position = FVector(Particle->GetX());
	const FQuat Rotation = FQuat(Particle->GetR());
	const FTransform WorldTransform(Rotation, Position);
	
	// 尝试获取关联的 PrimitiveComponent（用于 Dynamic 刚体的反作用力）
	UPrimitiveComponent* OwnerComponent = nullptr;
	if (ObjectState == Chaos::EObjectStateType::Dynamic || ObjectState == Chaos::EObjectStateType::Kinematic)
	{
		// 通过 Proxy 获取 OwnerComponent
		if (IPhysicsProxyBase* Proxy = Particle->GetProxy())
		{
			if (UObject* Owner = Proxy->GetOwner())
			{
				OwnerComponent = Cast<UPrimitiveComponent>(Owner);
			}
		}
	}
	
	// 添加到候选列表
	Candidates.Emplace(Particle, ObjectState, WorldTransform, Geometry, OwnerComponent);
	
	UE_LOG(LogEdenRopeBVH, Verbose, TEXT("BVH Overlap: Found candidate at %s, State=%d, Component=%s"), 
		*Position.ToString(), static_cast<int32>(ObjectState),
		OwnerComponent ? *OwnerComponent->GetName() : TEXT("None"));
	
	return true;
}

bool FEdenRopeOverlapVisitor::ShouldCollideWithEdenParticle(Chaos::FGeometryParticle* Particle) const
{
	if (!Particle)
	{
		return false;
	}
	
	const Chaos::FShapesArray& Shapes = Particle->ShapesArray();
	
	// 检查每个 Shape 的碰撞响应
	for (const TUniquePtr<Chaos::FPerShapeData>& Shape : Shapes)
	{
		if (!Shape)
		{
			continue;
		}
		
		// 检查 Sim 是否启用
		if (!Shape->GetSimEnabled())
		{
			continue;
		}
		
		const uint32 ChannelIndex = static_cast<uint32>(EdenParticleChannel);
		const uint64 ChannelBit = 1ull << ChannelIndex;

#if UE_VERSION_NEWER_THAN(5, 7, 0)
		// UE 5.8+ 使用 Chaos::Filter::FCombinedShapeFilterData
		const Chaos::Filter::FCombinedShapeFilterData& CombinedFilterData = Shape->GetCombinedShapeFilterData();
		if (!CombinedFilterData.IsValid())
		{
			return true;
		}

		const Chaos::Filter::FShapeFilterData& ShapeFilterData = CombinedFilterData.GetShapeFilterData();
		if ((ShapeFilterData.GetBlockChannels() & ChannelBit) != 0)
		{
			return true;
		}
#else
		const FCollisionFilterData& SimData = Shape->GetSimData();
		if (SimData.Word0 == 0 && SimData.Word1 == 0 && SimData.Word2 == 0 && SimData.Word3 == 0)
		{
			return true;
		}

		const uint32 LegacyChannelBit = 1u << ChannelIndex;
		if ((SimData.Word1 & LegacyChannelBit) != 0)
		{
			return true;
		}
#endif
	}
	
	return false;
}

} // namespace Eden
