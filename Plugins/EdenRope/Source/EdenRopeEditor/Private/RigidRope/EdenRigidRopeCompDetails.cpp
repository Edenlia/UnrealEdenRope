// Copyright Eden Games. All Rights Reserved.

#include "EdenRigidRopeCompDetails.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailPropertyRow.h"
#include "Components/PrimitiveComponent.h"
#include "RigidRope/EdenRigidRopeComponent.h"
#include "PhysicsEngine/BodyInstance.h"

#define LOCTEXT_NAMESPACE "EdenRigidRopeCompDetails"

TSharedRef<IDetailCustomization> FEdenRigidRopeCompDetails::MakeInstance()
{
	return MakeShareable(new FEdenRigidRopeCompDetails);
}

void FEdenRigidRopeCompDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// UEdenRigidRopeComponent 的 UCLASS 中设置了 HideCategories=(Physics)，
	// 这会阻止 FPrimitiveComponentDetails::AddPhysicsCategory 被调用，
	// 从而阻止 FBodyInstanceCustomizationHelper 添加 Mass 等属性。
	//
	// 这里我们手动重建 Physics 分类，只添加需要的属性（不包含 Mass）。
	// 参考 FSkeletalMeshComponentDetails 的做法。

	TSharedRef<IPropertyHandle> BodyInstanceHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(UPrimitiveComponent, BodyInstance));

	if (!BodyInstanceHandle->IsValidHandle())
	{
		return;
	}

	IDetailCategoryBuilder& PhysicsCategory = DetailBuilder.EditCategory("Physics");

	// Simulate Physics
	TSharedPtr<IPropertyHandle> SimulatePhysicsHandle = BodyInstanceHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FBodyInstance, bSimulatePhysics));
	if (SimulatePhysicsHandle.IsValid())
	{
		PhysicsCategory.AddProperty(SimulatePhysicsHandle);
	}

	// Enable Gravity
	TSharedPtr<IPropertyHandle> EnableGravityHandle = BodyInstanceHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FBodyInstance, bEnableGravity));
	if (EnableGravityHandle.IsValid())
	{
		PhysicsCategory.AddProperty(EnableGravityHandle);
	}

	// Linear Damping
	TSharedPtr<IPropertyHandle> LinearDampingHandle = BodyInstanceHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FBodyInstance, LinearDamping));
	if (LinearDampingHandle.IsValid())
	{
		PhysicsCategory.AddProperty(LinearDampingHandle);
	}

	// Angular Damping
	TSharedPtr<IPropertyHandle> AngularDampingHandle = BodyInstanceHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FBodyInstance, AngularDamping));
	if (AngularDampingHandle.IsValid())
	{
		PhysicsCategory.AddProperty(AngularDampingHandle);
	}

	// TODO: 后续需要隐藏Mass属性的话,参考 FBodyInstanceCustomizationHelper::UpdateFilters
}

#undef LOCTEXT_NAMESPACE
