// Copyright Eden Games. All Rights Reserved.

#include "EdenRigidRopeEditorViewportClient.h"
#include "RigidRope/Preview/SEdenRigidRopeEditorViewport.h"
#include "RigidRope/EdenRigidRopeAsset.h"
#include "AdvancedPreviewScene.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Materials/MaterialInstanceDynamic.h"

FEdenRigidRopeEditorViewportClient::FEdenRigidRopeEditorViewportClient(
	FAdvancedPreviewScene& InPreviewScene,
	const TSharedRef<SEdenRigidRopeEditorViewport>& InViewport,
	UEdenRigidRopeAsset* InAsset)
	: FEditorViewportClient(nullptr, &InPreviewScene, StaticCastSharedRef<SEditorViewport>(InViewport))
	, RopeAsset(InAsset)
{
	SetRealtime(true);

	// Translucent material for body debug draw (same material as PhAT editor)
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EditorMaterials/PhAT_UnselectedMaterial.PhAT_UnselectedMaterial"));
	if (BaseMat)
	{
		BodyMID = UMaterialInstanceDynamic::Create(BaseMat, GetTransientPackage());
	}

	// Camera setup
	SetViewLocation(FVector(-150.f, 0.f, 50.f));
	SetViewRotation(FRotator(-10.f, 0.f, 0.f));
}

void FEdenRigidRopeEditorViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(BodyMID);
}

void FEdenRigidRopeEditorViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	if (FAdvancedPreviewScene* Scene = static_cast<FAdvancedPreviewScene*>(PreviewScene))
	{
		Scene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
	}
}

void FEdenRigidRopeEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

	if (!RopeAsset.IsValid())
	{
		return;
	}

	DrawBodies(PDI);
	DrawConstraints(PDI);
}

FTransform FEdenRigidRopeEditorViewportClient::GetRestBodyTransform(int32 BodyIndex) const
{
	if (!RopeAsset.IsValid())
	{
		return FTransform::Identity;
	}

	const float SegLen = RopeAsset->GetSegmentLength();

	// 根据 PreviewForwardAxis 获取前向方向向量
	FVector ForwardDir;
	switch (RopeAsset->PreviewForwardAxis)
	{
		case EEdenRopeForwardAxis::PositiveX: ForwardDir = FVector( 1, 0, 0); break;
		case EEdenRopeForwardAxis::NegativeX: ForwardDir = FVector(-1, 0, 0); break;
		case EEdenRopeForwardAxis::PositiveY: ForwardDir = FVector( 0, 1, 0); break;
		case EEdenRopeForwardAxis::NegativeY: ForwardDir = FVector( 0,-1, 0); break;
		case EEdenRopeForwardAxis::PositiveZ: ForwardDir = FVector( 0, 0, 1); break;
		case EEdenRopeForwardAxis::NegativeZ: ForwardDir = FVector( 0, 0,-1); break;
		default: ForwardDir = FVector(1, 0, 0); break;
	}

	// Body 原点位于 Particle 端点（与 BodyInstance 空间一致）
	// Capsule 的 Center 偏移已在 BodySetup 中设置，绘制时由 Sphyl.GetTransform() 自动应用
	const FVector LocalCenter = ForwardDir * (BodyIndex * SegLen);

	// BodySetup 中的碰撞体沿 X 轴定义，需要旋转 Body 使其 X 轴对齐到 ForwardDir
	const FQuat BodyRot = FQuat::FindBetweenNormals(FVector::ForwardVector, ForwardDir);

	const FTransform PreviewOriginTM(RopeAsset->PreviewRotation.Quaternion(), RopeAsset->PreviewOrigin);
	return FTransform(BodyRot, LocalCenter) * PreviewOriginTM;
}

void FEdenRigidRopeEditorViewportClient::DrawBodies(FPrimitiveDrawInterface* PDI)
{
	UEdenRigidRopeAsset* Asset = RopeAsset.Get();
	if (!Asset)
	{
		return;
	}

	const FColor WireColor(180, 200, 255, 200);

	for (int32 i = 0; i < Asset->BodySetups.Num(); ++i)
	{
		UBodySetup* BS = Asset->BodySetups[i];
		if (!BS)
		{
			continue;
		}

		const FTransform BodyTM = GetRestBodyTransform(i);

		for (const FKSphylElem& Sphyl : BS->AggGeom.SphylElems)
		{
			FTransform ElemTM = Sphyl.GetTransform() * BodyTM;
			FVector Scale(1.0f);

			if (BodyMID)
			{
				Sphyl.DrawElemSolid(PDI, ElemTM, Scale, BodyMID->GetRenderProxy());
			}
			Sphyl.DrawElemWire(PDI, ElemTM, Scale, WireColor);
		}

		for (const FKSphereElem& Sphere : BS->AggGeom.SphereElems)
		{
			FTransform ElemTM = Sphere.GetTransform() * BodyTM;
			FVector Scale(1.0f);

			if (BodyMID)
			{
				Sphere.DrawElemSolid(PDI, ElemTM, Scale, BodyMID->GetRenderProxy());
			}
			Sphere.DrawElemWire(PDI, ElemTM, Scale, WireColor);
		}

		for (const FKBoxElem& Box : BS->AggGeom.BoxElems)
		{
			FTransform ElemTM = Box.GetTransform() * BodyTM;
			FVector Scale(1.0f);

			if (BodyMID)
			{
				Box.DrawElemSolid(PDI, ElemTM, Scale, BodyMID->GetRenderProxy());
			}
			Box.DrawElemWire(PDI, ElemTM, Scale, WireColor);
		}
	}
}

void FEdenRigidRopeEditorViewportClient::DrawConstraints(FPrimitiveDrawInterface* PDI)
{
	UEdenRigidRopeAsset* Asset = RopeAsset.Get();
	if (!Asset)
	{
		return;
	}

	// Match Physics Asset Editor defaults:
	//   Scale = 1.0 → Length = UnselectedJointRenderSize(4) * Scale
	//   LimitDrawScale = ConstraintArrowScale(60) * ConstraintDrawSize(1.0)
	constexpr float ConstraintScale = 1.0f;
	constexpr float ConstraintArrowScale = 60.0f;
	const float LimitDrawScale = ConstraintArrowScale * ConstraintScale;

	for (int32 i = 0; i < Asset->ConstraintSetup.Num(); ++i)
	{
		UPhysicsConstraintTemplate* CT = Asset->ConstraintSetup[i];
		if (!CT)
		{
			continue;
		}

		const FConstraintInstance& CI = CT->DefaultInstance;

		const int32 Body1Idx = Asset->FindBodyIndex(CI.ConstraintBone1);
		const int32 Body2Idx = Asset->FindBodyIndex(CI.ConstraintBone2);
		if (Body1Idx == INDEX_NONE || Body2Idx == INDEX_NONE)
		{
			continue;
		}

		const FTransform Body1TM = GetRestBodyTransform(Body1Idx);
		const FTransform Body2TM = GetRestBodyTransform(Body2Idx);

		FTransform Frame1 = FTransform(FQuat::Identity, CI.Pos1) * Body1TM;
		FTransform Frame2 = FTransform(FQuat::Identity, CI.Pos2) * Body2TM;
		Frame1.RemoveScaling();
		Frame2.RemoveScaling();

		CI.DrawConstraint(PDI, ConstraintScale, LimitDrawScale, true, false, Frame1, Frame2, false, false);
	}
}
