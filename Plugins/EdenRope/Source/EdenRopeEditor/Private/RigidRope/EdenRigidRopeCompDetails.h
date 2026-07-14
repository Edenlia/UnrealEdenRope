// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/**
 * Detail customization for UEdenRigidRopeComponent.
 * 
 * UEdenRigidRopeComponent 的 UCLASS 中设置了 HideCategories=(Physics)，
 * 阻止 FPrimitiveComponentDetails 通过 FBodyInstanceCustomizationHelper 添加 Physics 分类属性。
 * 本 Customization 手动重建 Physics 分类，只添加需要的属性（不包含 Mass 相关属性）。
 * 参考 FSkeletalMeshComponentDetails + FBodyInstanceCustomizationHelper::UpdateFilters 的做法。
 */
class FEdenRigidRopeCompDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
