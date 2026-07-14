// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialRenderProxy.h"
#include "RenderingThread.h"
#include "Misc/EngineVersionComparison.h"
#include "DynamicMeshBuilder.h"

class UPrimitiveComponent;
class IEdenRopeInterface;
class UBodySetup;

/** Index Buffer */
class FEdenRopeRenderIndexBuffer : public FIndexBuffer
{
public:
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
#if UE_VERSION_NEWER_THAN(5, 7, 0)
		// UE5.8: FRHIResourceCreateInfo + FRHICommandList::CreateIndexBuffer were removed.
		// The modern path builds an FRHIBufferCreateDesc and calls CreateBuffer().
		const FRHIBufferCreateDesc CreateDesc =
			FRHIBufferCreateDesc::CreateIndex<int32>(TEXT("FEdenRopeRenderIndexBuffer"), NumIndices)
			.AddUsage(EBufferUsageFlags::Dynamic)
			.DetermineInitialState();
		IndexBufferRHI = RHICmdList.CreateBuffer(CreateDesc);
#else
		FRHIResourceCreateInfo CreateInfo(TEXT("FEdenRopeRenderIndexBuffer"));
		IndexBufferRHI = RHICmdList.CreateIndexBuffer(sizeof(int32), NumIndices * sizeof(int32), BUF_Dynamic, CreateInfo);
#endif
	}

	int32 NumIndices;
};

/** Dynamic data sent to render thread */
struct FEdenRopeRenderDynamicData
{
	/** Array of points */
	TArray<FVector> RopePoints;
	/** Array of body transforms for collision visualization */
	TArray<FTransform> BodyTransforms;
	/** Per-particle orientations (optional: empty for auto parallel transport, populated for oriented particles) */
	TArray<FQuat> RopeOrientations;
};

/** Path frame along the rope, carrying a full tangent frame + metadata for mesh extrusion. */
struct FEdenRopePathFrame
{
	FVector Position  = FVector::ZeroVector;
	FVector Tangent   = FVector::ForwardVector;
	FVector Normal    = FVector::UpVector;     // "Up" direction
	FVector Binormal  = FVector::RightVector;  // "Left" direction
	float   Thickness = 1.0f;

	/** Weighted sum: Result = W1*A + W2*B + W3*C  (used by Chaikin smoothing) */
	static FEdenRopePathFrame WeightedSum(float W1, const FEdenRopePathFrame& A,
	                                       float W2, const FEdenRopePathFrame& B,
	                                       float W3, const FEdenRopePathFrame& C)
	{
		FEdenRopePathFrame Out;
		Out.Position  = W1 * A.Position  + W2 * B.Position  + W3 * C.Position;
		Out.Tangent   = W1 * A.Tangent   + W2 * B.Tangent   + W3 * C.Tangent;
		Out.Normal    = W1 * A.Normal    + W2 * B.Normal    + W3 * C.Normal;
		Out.Binormal  = W1 * A.Binormal  + W2 * B.Binormal  + W3 * C.Binormal;
		Out.Thickness = W1 * A.Thickness + W2 * B.Thickness + W3 * C.Thickness;
		return Out;
	}
};

DECLARE_RENDER_COMMAND_PIPE(EdenRope, EDENROPE_API);

/**
 * Scene proxy for rendering rope mesh and collision visualization.
 * Supports two tangent frame modes:
 *   - Parallel Transport (default): auto-computed from positions, minimal twist
 *   - Oriented Particles: tangent frame extracted from per-particle quaternions
 */
class FEdenRopeSceneProxy : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FEdenRopeSceneProxy(UPrimitiveComponent* Component);
	virtual ~FEdenRopeSceneProxy();

	/** Compute the number of output frames after Chaikin smoothing. */
	static int32 GetChaikinOutputCount(int32 InputCount, uint8 Smoothing)
	{
		if (Smoothing == 0 || InputCount < 3)
			return InputCount;
		const int32 PCount = 1 << Smoothing; // 2^k
		return (InputCount - 2) * PCount + 2;
	}

	int32 GetRequiredVertexCount() const
	{
		return SmoothFrameCount * (RopeSideNum + 1);
	}

	int32 GetRequiredIndexCount() const
	{
		return (SmoothFrameCount - 1) * RopeSideNum * 2 * 3;
	}

	int32 GetVertIndex(int32 AlongIdx, int32 AroundIdx) const
	{
		return (AlongIdx * (RopeSideNum + 1)) + AroundIdx;
	}

	// ===== Path smoothing pipeline =====

	/** Generate raw PathFrames from particle positions/orientations. */
	void GenerateRawFrames(const TArray<FVector>& InPoints, const TArray<FQuat>& InOrientations, TArray<FEdenRopePathFrame>& OutFrames);

	/** Decimate frames in-place using Ramer-Douglas-Peucker-like curvature threshold. */
	static void DecimateFrames(TArray<FEdenRopePathFrame>& Frames, float DecimationThreshold);

	/** Apply Chaikin closed-form smoothing (non-recursive). */
	static void ChaikinSmooth(const TArray<FEdenRopePathFrame>& InFrames, uint8 Smoothing, TArray<FEdenRopePathFrame>& OutFrames);

	void BuildRopeMesh(const FEdenRopeRenderDynamicData* DynamicData, TArray<FDynamicMeshVertex>& OutVertices, TArray<int32>& OutIndices, bool bRebuildIndices);

	/** Called on render thread to assign new dynamic data */
	void SetDynamicData_RenderThread(FRHICommandListBase& RHICmdList, FEdenRopeRenderDynamicData* NewDynamicData);

	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }
	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

private:
	UMaterialInterface* Material;
	FStaticMeshVertexBuffers VertexBuffers;
	FEdenRopeRenderIndexBuffer IndexBuffer;
	FLocalVertexFactory VertexFactory;

	FMaterialRelevance MaterialRelevance;

	int32 RopeSegmentNum;     // Raw simulation segment count (NumParticles - 1)
	float RopeWidth;
	int32 RopeSideNum;
	float TileMaterial;
	uint8 Smoothing;          // Chaikin smoothing iterations (0-3)
	float Decimation;         // Decimation curvature threshold (0 = no decimation)
	int32 SmoothFrameCount;   // Actual render frame count after smoothing

	// Whether this proxy uses oriented particles (quaternion-based tangent frame)
	bool bUseOrientedParticles;

	// Cached CPU-side arrays (pre-allocated, reused every frame)
	TArray<FDynamicMeshVertex> CachedVertices;
	TArray<int32> CachedIndices;
	bool bIndicesDirty;

	// Collision visualization data
	bool bNeedCollisionRender;
	UBodySetup* BodySetup;
	TArray<FTransform> BodyTransforms;
	FCollisionResponseContainer CollisionResponse;
};
