// Fill out your copyright notice in the Description page of Project Settings.

#include "EdenRopeSceneProxy.h"
#include "Interface/EdenRopeInterface.h"
#include "PrimitiveViewRelevance.h"
#include "MaterialDomain.h"
#include "SceneManagement.h"
#include "Materials/Material.h"
#include "Engine/Engine.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/AggregateGeom.h"

DEFINE_RENDER_COMMAND_PIPE(EdenRope, ERenderCommandPipeFlags::None);

//////////////////////////////////////////////////////////////////////////
// FEdenRopeSceneProxy

FEdenRopeSceneProxy::FEdenRopeSceneProxy(UPrimitiveComponent* Component)
	: FPrimitiveSceneProxy(Component)
	, Material(NULL)
	, VertexFactory(GetScene().GetFeatureLevel(), "FEdenRopeSceneProxy")
	, RopeSegmentNum(0)
	, RopeWidth(1.0f)
	, RopeSideNum(4)
	, TileMaterial(1.0f)
	, Smoothing(0)
	, Decimation(0.0f)
	, SmoothFrameCount(0)
	, bUseOrientedParticles(false)
	, bIndicesDirty(true)
	, bNeedCollisionRender(false)
	, BodySetup(nullptr)
	, CollisionResponse(Component->GetCollisionResponseToChannels())
{
	// Get data through interface
	IEdenRopeInterface* RopeInterface = Cast<IEdenRopeInterface>(Component);
	check(RopeInterface);

	// Get rendering parameters from interface
	RopeSegmentNum = RopeInterface->GetRopeSegmentNum();
	RopeWidth = RopeInterface->GetRopeCableWidth();
	RopeSideNum = RopeInterface->GetRopeNumSides();
	TileMaterial = RopeInterface->GetRopeTileMaterial();
	Smoothing = RopeInterface->GetRopeSmoothing();
	Decimation = RopeInterface->GetRopeDecimation();
	MaterialRelevance = RopeInterface->GetRopeMaterialRelevance();
	bUseOrientedParticles = RopeInterface->UsesOrientedParticles();

	// Compute the maximum possible smooth frame count for buffer pre-allocation.
	// Decimation can only reduce points, so worst-case is no decimation.
	const int32 RawFrameCount = RopeSegmentNum + 1;
	SmoothFrameCount = GetChaikinOutputCount(RawFrameCount, Smoothing);

	// Get material
	Material = RopeInterface->GetRopeMaterial();
	if (Material == NULL)
	{
		Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	// Get collision visualization data if needed
	bNeedCollisionRender = RopeInterface->NeedRigidDebugRender();
	if (bNeedCollisionRender)
	{
		BodySetup = RopeInterface->GetRopeBodySetup();
		RopeInterface->GetRopeBodyTransforms(BodyTransforms);
	}

	// Pre-allocate cached CPU-side arrays
	CachedVertices.SetNum(GetRequiredVertexCount());
	CachedIndices.SetNum(GetRequiredIndexCount());

	VertexBuffers.InitWithDummyData(&UE::RenderCommandPipe::EdenRope, &VertexFactory, GetRequiredVertexCount());
	IndexBuffer.NumIndices = GetRequiredIndexCount();

	ENQUEUE_RENDER_COMMAND(InitRopeResources)(UE::RenderCommandPipe::EdenRope,
		[this](FRHICommandList& RHICmdList)
		{
			IndexBuffer.InitResource(RHICmdList);
		});
}

FEdenRopeSceneProxy::~FEdenRopeSceneProxy()
{
	VertexBuffers.PositionVertexBuffer.ReleaseResource();
	VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
	VertexBuffers.ColorVertexBuffer.ReleaseResource();
	IndexBuffer.ReleaseResource();
	VertexFactory.ReleaseResource();
}

// ===== Path Smoothing Pipeline =====

void FEdenRopeSceneProxy::GenerateRawFrames(const TArray<FVector>& InPoints, const TArray<FQuat>& InOrientations, TArray<FEdenRopePathFrame>& OutFrames)
{
	const int32 NumPoints = InPoints.Num();
	OutFrames.SetNum(NumPoints);

	const bool bHasOrientations = InOrientations.Num() == NumPoints;

	if (bHasOrientations)
	{
		// Path A: Extract tangent frame from per-particle quaternion
		for (int32 i = 0; i < NumPoints; i++)
		{
			FEdenRopePathFrame& Frame = OutFrames[i];
			const FQuat& Q = InOrientations[i];
			Frame.Position = InPoints[i];
			Frame.Tangent  = Q.GetAxisZ();   // Z = forward along rope
			Frame.Normal   = Q.GetAxisX();   // X = up/normal
			Frame.Binormal = Q.GetAxisY();   // Y = left/binormal
			Frame.Thickness = RopeWidth;
		}
	}
	else
	{
		// Path B: Parallel Transport (minimal twist, auto-computed from positions)
		TArray<FVector> TangentDirs;
		TangentDirs.SetNumUninitialized(NumPoints);

		// 1. Compute tangent via central difference
		for (int32 i = 0; i < NumPoints; i++)
		{
			const int32 PrevIndex = FMath::Max(0, i - 1);
			const int32 NextIndex = FMath::Min(i + 1, NumPoints - 1);
			TangentDirs[i] = (InPoints[NextIndex] - InPoints[PrevIndex]).GetSafeNormal();
		}

		// 2. Initialize first frame
		FVector Normal = FVector::XAxisVector;
		if (FMath::Abs(FVector::DotProduct(TangentDirs[0], Normal)) > 0.99f)
		{
			Normal = FVector::YAxisVector;
		}
		Normal = (Normal - FVector::DotProduct(Normal, TangentDirs[0]) * TangentDirs[0]).GetSafeNormal();

		FVector Up   = Normal;
		FVector Left = FVector::CrossProduct(TangentDirs[0], Up);

		OutFrames[0].Position  = InPoints[0];
		OutFrames[0].Tangent   = TangentDirs[0];
		OutFrames[0].Normal    = Up;
		OutFrames[0].Binormal  = Left;
		OutFrames[0].Thickness = RopeWidth;

		// 3. Parallel transport along the rope
		for (int32 i = 1; i < NumPoints; i++)
		{
			const FQuat RotQ = FQuat::FindBetweenNormals(TangentDirs[i - 1], TangentDirs[i]);
			Up   = RotQ.RotateVector(Up);
			Left = RotQ.RotateVector(Left);

			FEdenRopePathFrame& Frame = OutFrames[i];
			Frame.Position  = InPoints[i];
			Frame.Tangent   = TangentDirs[i];
			Frame.Normal    = Up;
			Frame.Binormal  = Left;
			Frame.Thickness = RopeWidth;
		}
	}
}

void FEdenRopeSceneProxy::DecimateFrames(TArray<FEdenRopePathFrame>& Frames, float DecimationThreshold)
{
	const int32 InputCount = Frames.Num();
	if (DecimationThreshold < KINDA_SMALL_NUMBER || InputCount < 3)
	{
		return; // nothing to decimate
	}

	const float ScaledThreshold = DecimationThreshold * DecimationThreshold * 0.01f;

	// In-place Ramer-Douglas-Peucker-like forward decimation (matching Obi's approach)
	TArray<FEdenRopePathFrame> Result;
	Result.Reserve(InputCount);

	int32 Start = 0;
	const int32 End = InputCount - 1;

	while (Start < End)
	{
		Result.Add(Frames[Start]);

		int32 NewEnd = End;
		while (true)
		{
			float MaxDistSq = 0.0f;
			int32 MaxDistIdx = 0;

			const FVector& SegStart = Frames[Start].Position;
			const FVector& SegEnd   = Frames[NewEnd].Position;

			for (int32 k = Start + 1; k < NewEnd; k++)
			{
				// Distance from point to line segment
				const FVector Nearest = FMath::ClosestPointOnSegment(Frames[k].Position, SegStart, SegEnd);
				const float DistSq = FVector::DistSquared(Nearest, Frames[k].Position);
				if (DistSq > MaxDistSq)
				{
					MaxDistSq = DistSq;
					MaxDistIdx = k;
				}
			}

			if (MaxDistSq <= ScaledThreshold)
				break;

			NewEnd = MaxDistIdx;
		}

		Start = NewEnd;
	}

	// Add the last point
	Result.Add(Frames[End]);

	Frames = MoveTemp(Result);
}

void FEdenRopeSceneProxy::ChaikinSmooth(const TArray<FEdenRopePathFrame>& InFrames, uint8 InSmoothing, TArray<FEdenRopePathFrame>& OutFrames)
{
	const int32 N = InFrames.Num();

	if (InSmoothing == 0 || N < 3)
	{
		OutFrames = InFrames;
		return;
	}

	const int32 k = InSmoothing;
	const int32 PCount = 1 << k;  // 2^k
	const int32 N0 = N - 1;       // number of input segments
	const int32 OutputCount = (N - 2) * PCount + 2;

	OutFrames.SetNum(OutputCount);

	// Precalculate powers
	const float TwoToMinusKPlus1  = FMath::Pow(2.0f, -(float)(k + 1));
	const float TwoToMinusK       = FMath::Pow(2.0f, -(float)k);
	const float TwoToMinus2K      = FMath::Pow(2.0f, -(float)(2 * k));
	const float TwoToMinus2KMinus1 = FMath::Pow(2.0f, -(float)(2 * k + 1));

	// Calculate internal points using Chaikin closed-form
	for (int32 j = 1; j <= PCount; ++j)
	{
		const float jf = (float)(j - 1);
		const float F = 0.5f - TwoToMinusKPlus1 - jf * (TwoToMinusK - (float)j * TwoToMinus2KMinus1);
		const float G = 0.5f + TwoToMinusKPlus1 + jf * (TwoToMinusK - (float)j * TwoToMinus2K);
		const float H = jf * (float)j * TwoToMinus2KMinus1;

		for (int32 l = 1; l < N0; ++l)
		{
			OutFrames[(l - 1) * PCount + j] = FEdenRopePathFrame::WeightedSum(
				F, InFrames[l - 1],
				G, InFrames[l],
				H, InFrames[l + 1]);
		}
	}

	// First and last frames match original endpoints exactly
	OutFrames[0] = InFrames[0];
	OutFrames[OutputCount - 1] = InFrames[N - 1];
}

// ===== Main mesh building =====

void FEdenRopeSceneProxy::BuildRopeMesh(const FEdenRopeRenderDynamicData* DynamicData, TArray<FDynamicMeshVertex>& OutVertices, TArray<int32>& OutIndices, bool bRebuildIndices)
{
	const TArray<FVector>& InPoints = DynamicData->RopePoints;
	const int32 NumPoints = InPoints.Num();

	if (NumPoints < 2)
	{
		return;
	}

	// ===== Stage 1: Generate raw path frames from particles =====
	TArray<FEdenRopePathFrame> RawFrames;
	GenerateRawFrames(InPoints, DynamicData->RopeOrientations, RawFrames);

	// ===== Stage 2: Decimation (optional) =====
	if (Decimation > 0.0f)
	{
		DecimateFrames(RawFrames, Decimation);
	}

	// ===== Stage 3: Chaikin smoothing =====
	TArray<FEdenRopePathFrame> SmoothFrames;
	ChaikinSmooth(RawFrames, Smoothing, SmoothFrames);

	const int32 ActualFrameCount = SmoothFrames.Num();

	// Clamp to pre-allocated buffer size (decimation may have reduced below max)
	const int32 RenderFrameCount = FMath::Min(ActualFrameCount, SmoothFrameCount);
	const int32 RenderSegmentCount = RenderFrameCount - 1;

	// ===== Stage 4: Extrude section along smooth frames =====
	const FColor VertexColor(255, 255, 255);
	const int32 NumRingVerts = RopeSideNum + 1;

	// UV: distribute tile material evenly over the original segment count range
	const float TotalRawSegments = (float)FMath::Max(RopeSegmentNum, 1);

	for (int32 FrameIdx = 0; FrameIdx < RenderFrameCount; FrameIdx++)
	{
		const FEdenRopePathFrame& Frame = SmoothFrames[FrameIdx];
		const FVector& Tangent  = Frame.Tangent;
		const FVector& Up       = Frame.Normal;
		const FVector& Left     = Frame.Binormal;

		// V coordinate: map smoothed frame index to original segment range for UV continuity
		const float AlongFrac = (RenderFrameCount > 1)
			? ((float)FrameIdx / (float)(RenderFrameCount - 1))
			: 0.0f;
		const float VCoord = AlongFrac * TotalRawSegments * TileMaterial;

		for (int32 VertIdx = 0; VertIdx < NumRingVerts; VertIdx++)
		{
			const float AroundFrac = (float)VertIdx / (float)RopeSideNum;
			const float RadAngle = 2.f * PI * AroundFrac;
			const FVector OutDir = (FMath::Cos(RadAngle) * Up) - (FMath::Sin(RadAngle) * Left);

			FDynamicMeshVertex& Vert = OutVertices[GetVertIndex(FrameIdx, VertIdx)];
			Vert.Position = FVector3f(Frame.Position + (OutDir * 0.5f * Frame.Thickness));
			Vert.TextureCoordinate[0] = FVector2f(VCoord, AroundFrac);
			Vert.Color = VertexColor;
			Vert.SetTangents((FVector3f)Tangent, FVector3f(OutDir ^ Tangent), (FVector3f)OutDir);
		}
	}

	// Zero out any unused vertices (when decimation reduced below max allocation)
	for (int32 i = RenderFrameCount * NumRingVerts; i < SmoothFrameCount * NumRingVerts; i++)
	{
		OutVertices[i].Position = FVector3f::ZeroVector;
	}

	// ===== Build indices (only when topology changes) =====
	if (bRebuildIndices)
	{
		int32 IdxOffset = 0;
		for (int32 SegIdx = 0; SegIdx < RenderSegmentCount; SegIdx++)
		{
			for (int32 SideIdx = 0; SideIdx < RopeSideNum; SideIdx++)
			{
				const int32 TL = GetVertIndex(SegIdx, SideIdx);
				const int32 BL = GetVertIndex(SegIdx, SideIdx + 1);
				const int32 TR = GetVertIndex(SegIdx + 1, SideIdx);
				const int32 BR = GetVertIndex(SegIdx + 1, SideIdx + 1);

				OutIndices[IdxOffset++] = TL;
				OutIndices[IdxOffset++] = BL;
				OutIndices[IdxOffset++] = TR;

				OutIndices[IdxOffset++] = TR;
				OutIndices[IdxOffset++] = BL;
				OutIndices[IdxOffset++] = BR;
			}
		}

		// Zero remaining indices (when decimation caused fewer segments than max)
		const int32 MaxIndices = GetRequiredIndexCount();
		for (int32 i = IdxOffset; i < MaxIndices; i++)
		{
			OutIndices[i] = 0;
		}
	}
}

void FEdenRopeSceneProxy::SetDynamicData_RenderThread(FRHICommandListBase& RHICmdList, FEdenRopeRenderDynamicData* NewDynamicData)
{
	if (NewDynamicData != nullptr)
	{
		// When decimation is active, topology may change every frame
		const bool bNeedFullRebuild = bIndicesDirty || (Decimation > 0.0f);

		// Build mesh using cached arrays (no per-frame heap allocation)
		BuildRopeMesh(NewDynamicData, CachedVertices, CachedIndices, bNeedFullRebuild);

		check(CachedVertices.Num() == GetRequiredVertexCount());
		check(CachedIndices.Num() == GetRequiredIndexCount());

		// Copy vertex data into GPU staging buffers
		for (int i = 0; i < CachedVertices.Num(); i++)
		{
			const FDynamicMeshVertex& Vertex = CachedVertices[i];
			VertexBuffers.PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
			VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(i, Vertex.TangentX.ToFVector3f(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector3f());
		}

		// Upload Position buffer (every frame)
		{
			auto& VertexBuffer = VertexBuffers.PositionVertexBuffer;
			void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
			RHICmdList.UnlockBuffer(VertexBuffer.VertexBufferRHI);
		}

		// Upload Tangent buffer (every frame)
		{
			auto& VertexBuffer = VertexBuffers.StaticMeshVertexBuffer;
			void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetTangentSize(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTangentData(), VertexBuffer.GetTangentSize());
			RHICmdList.UnlockBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI);
		}

		// Upload Color, TexCoord, and Index buffers when topology is dirty
		// (first frame, or every frame when decimation is active)
		if (bNeedFullRebuild)
		{
			// Set UV and Color into staging buffers
			for (int i = 0; i < CachedVertices.Num(); i++)
			{
				const FDynamicMeshVertex& Vertex = CachedVertices[i];
				VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 0, Vertex.TextureCoordinate[0]);
				VertexBuffers.ColorVertexBuffer.VertexColor(i) = Vertex.Color;
			}

			// Upload Color buffer
			{
				auto& VertexBuffer = VertexBuffers.ColorVertexBuffer;
				void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
				FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
				RHICmdList.UnlockBuffer(VertexBuffer.VertexBufferRHI);
			}

			// Upload TexCoord buffer
			{
				auto& VertexBuffer = VertexBuffers.StaticMeshVertexBuffer;
				void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetTexCoordSize(), RLM_WriteOnly);
				FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTexCoordData(), VertexBuffer.GetTexCoordSize());
				RHICmdList.UnlockBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI);
			}

			// Upload Index buffer
			void* IndexBufferData = RHICmdList.LockBuffer(IndexBuffer.IndexBufferRHI, 0, CachedIndices.Num() * sizeof(int32), RLM_WriteOnly);
			FMemory::Memcpy(IndexBufferData, CachedIndices.GetData(), CachedIndices.Num() * sizeof(int32));
			RHICmdList.UnlockBuffer(IndexBuffer.IndexBufferRHI);

			bIndicesDirty = false;
		}

		// Update body transforms for collision visualization
		BodyTransforms = MoveTemp(NewDynamicData->BodyTransforms);

		delete NewDynamicData;
		NewDynamicData = NULL;
	}
}

void FEdenRopeSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	checkSlow(IsInParallelRenderingThread());

	if (!HasViewDependentDPG())
	{
		FMeshBatch Mesh;
		Mesh.VertexFactory = &VertexFactory;
		Mesh.MaterialRenderProxy = Material->GetRenderProxy();
		Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
		Mesh.Type = PT_TriangleList;
		Mesh.DepthPriorityGroup = SDPG_World;
		Mesh.MeshIdInPrimitive = 0;
		Mesh.LODIndex = 0;
		Mesh.SegmentIndex = 0;

		FMeshBatchElement& BatchElement = Mesh.Elements[0];
		BatchElement.IndexBuffer = &IndexBuffer;
		BatchElement.FirstIndex = 0;
		BatchElement.NumPrimitives = GetRequiredIndexCount() / 3;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = GetRequiredVertexCount();

		PDI->DrawMesh(Mesh, FLT_MAX);
	}
}

void FEdenRopeSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RopeSceneProxy_GetDynamicMeshElements);

	const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

	auto WireframeMaterialInstance = new FColoredMaterialRenderProxy(
		GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL,
		FLinearColor(0, 0.5f, 1.f)
	);

	Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);

	FMaterialRenderProxy* MaterialProxy = NULL;
	if (bWireframe)
	{
		MaterialProxy = WireframeMaterialInstance;
	}
	else
	{
		MaterialProxy = Material->GetRenderProxy();
	}

	// Check if we're in collision view mode
	const bool bInCollisionView = ViewFamily.EngineShowFlags.CollisionVisibility || ViewFamily.EngineShowFlags.CollisionPawn;
	const bool bDrawCollision = ViewFamily.EngineShowFlags.Collision && IsCollisionEnabled();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

			// Skip drawing mesh in collision view mode
			if (!bInCollisionView)
			{
				// Draw the mesh.
				FMeshBatch& Mesh = Collector.AllocateMesh();
				FMeshBatchElement& BatchElement = Mesh.Elements[0];
				BatchElement.IndexBuffer = &IndexBuffer;
				Mesh.bWireframe = bWireframe;
				Mesh.VertexFactory = &VertexFactory;
				Mesh.MaterialRenderProxy = MaterialProxy;

				bool bHasPrecomputedVolumetricLightmap;
				FMatrix PreviousLocalToWorld;
				int32 SingleCaptureIndex;
				bool bOutputVelocity;
				GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);
				bOutputVelocity |= AlwaysHasVelocity();

				FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
				DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity, GetCustomPrimitiveData());
				BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

				BatchElement.FirstIndex = 0;
				BatchElement.NumPrimitives = GetRequiredIndexCount() / 3;
				BatchElement.MinVertexIndex = 0;
				BatchElement.MaxVertexIndex = GetRequiredVertexCount();
				Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.bCanApplyViewModeOverrides = false;
				Collector.AddMesh(ViewIndex, Mesh);
			}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			// Render bounds
			RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());

			// Draw collision visualization
			if ((bDrawCollision || bInCollisionView) && BodySetup && BodySetup->AggGeom.GetElementCount() > 0)
			{
				// Check if we should draw based on collision response
				bool bHasResponse = ViewFamily.EngineShowFlags.CollisionPawn && CollisionResponse.GetResponse(ECC_Pawn) != ECR_Ignore;
				bHasResponse |= ViewFamily.EngineShowFlags.CollisionVisibility && CollisionResponse.GetResponse(ECC_Visibility) != ECR_Ignore;

				if (bHasResponse || bDrawCollision)
				{
					// Get collision color
					const FColor CollisionColor = FColor(157, 149, 223, 255); // Light purple for rope collision

					// Draw collision for each body
					for (const FTransform& BodyTransform : BodyTransforms)
					{
						// Draw wireframe collision
						BodySetup->AggGeom.GetAggGeom(BodyTransform, CollisionColor, nullptr, false, false, AlwaysHasVelocity(), ViewIndex, Collector);
					}
				}
			}
#endif
		}
	}
}

FPrimitiveViewRelevance FEdenRopeSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;

	const bool bInCollisionView = View->Family->EngineShowFlags.CollisionVisibility || View->Family->EngineShowFlags.CollisionPawn;
	const bool bAllowStaticLighting = IsStaticLightingAllowed();

	if (
#if !(UE_BUILD_SHIPPING) || WITH_EDITOR
		IsRichView(*View->Family) ||
		View->Family->EngineShowFlags.Collision ||
		bInCollisionView ||
		View->Family->EngineShowFlags.Bounds ||
		View->Family->EngineShowFlags.VisualizeInstanceUpdates ||
#endif
#if WITH_EDITOR
		(IsSelected() && View->Family->EngineShowFlags.VertexColors) ||
		(IsSelected() && View->Family->EngineShowFlags.PhysicalMaterialMasks) ||
#endif
		// Force down dynamic rendering path if invalid lightmap settings, so we can apply an error material in DrawRichMesh
		(bAllowStaticLighting && HasStaticLighting() && !HasValidSettingsForStaticLighting()) ||
		HasViewDependentDPG()
		)
	{
		Result.bDynamicRelevance = true;
	}
	else
	{
		Result.bStaticRelevance = true;

#if WITH_EDITOR
		//only check these in the editor
		Result.bEditorVisualizeLevelInstanceRelevance = IsEditingLevelInstanceChild();
		Result.bEditorStaticSelectionRelevance = (IsSelected() || IsHovered());
#endif
	}

	// Ensure we are visible in collision view
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (View->Family->EngineShowFlags.Collision || bInCollisionView)
	{
		Result.bDrawRelevance = true;
	}
#endif

	MaterialRelevance.SetPrimitiveViewRelevance(Result);

	Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;

	return Result;
}
