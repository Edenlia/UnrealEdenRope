#pragma once

#include "UObject/Interface.h"

#if WITH_EDITOR
#include "DetailLayoutBuilder.h"
#endif

#include "EdenRopeInterface.generated.h"


UINTERFACE(MinimalAPI)
class UEdenRopeInterface : public UInterface
{
	GENERATED_BODY()
};

class EDENROPE_API IEdenRopeInterface
{
	GENERATED_BODY()

public:
	// Render Init needed
	virtual int32 GetRopeSegmentNum() const = 0;
	virtual float GetRopeCableWidth() const = 0;
	virtual int32 GetRopeNumSides() const = 0;
	virtual float GetRopeTileMaterial() const = 0;
	virtual UMaterialInterface* GetRopeMaterial() const = 0;
	virtual FMaterialRelevance GetRopeMaterialRelevance() const = 0;
    
	// Path smoothing: Chaikin iterations (0 = none, up to 3)
	virtual uint8 GetRopeSmoothing() const { return 0; }
	// Path decimation: curvature threshold (0 = no decimation, higher = more aggressive)
	virtual float GetRopeDecimation() const { return 0.0f; }

	// Oriented particles: whether this component uses per-particle quaternion for tangent frame
	virtual bool UsesOrientedParticles() const { return false; }

	// RigidBody preview needed: For Rigid Rope, Debug for collision
	virtual void GetRopeParticleLocations(TArray<FVector>& OutLocations) const = 0;
	virtual bool NeedRigidDebugRender() const { return false; }
	virtual UBodySetup* GetRopeBodySetup() const { return nullptr; }
	virtual void GetRopeBodyTransforms(TArray<FTransform>& OutTransforms) const {}

	// 获取绳索首尾端点的世界坐标
	virtual FVector GetRopeStartWorldLocation() const { return FVector::ZeroVector; }
	virtual FVector GetRopeEndWorldLocation() const { return FVector::ZeroVector; }
};