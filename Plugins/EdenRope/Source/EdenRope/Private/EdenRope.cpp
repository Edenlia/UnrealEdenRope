// Copyright Eden Games. All Rights Reserved.

#include "EdenRope.h"
#include "EdenRopeCollisionChannel.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/CollisionProfile.h"
#include "EdenRopeSettings.h"
#include "Logging/MessageLog.h"
#define LOCTEXT_NAMESPACE "EdenRope"
#endif

void FEdenRopeModule::StartupModule()
{
	// 初始化 EdenParticle 碰撞通道（从配置中动态查找）
	Eden::InitializeEdenParticleChannel();

#if WITH_EDITOR
	if (GIsEditor)
	{
		PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(
			this, &FEdenRopeModule::OnPreBeginPIE);
		PostBeginPIEHandle = FEditorDelegates::PostPIEStarted.AddRaw(
			this, &FEdenRopeModule::OnPostBeginPIE);
		PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
			this, &FEdenRopeModule::OnObjectPropertyChanged);
	}
#endif
}

void FEdenRopeModule::ShutdownModule()
{
#if WITH_EDITOR
	FEditorDelegates::PostPIEStarted.Remove(PreBeginPIEHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
#endif
}

#if WITH_EDITOR
void FEdenRopeModule::OnPreBeginPIE(const bool bIsSimulating)
{
	// 每次 PIE 开始前重新初始化（顺便拿到最新设置/collision profile）
	Eden::InitializeEdenParticleChannel();
}

void FEdenRopeModule::OnPostBeginPIE(const bool bIsSimulating)
{
	if (!Eden::bEdenParticleChannelValid)
	{
		const UEdenRopeSettings* Settings = UEdenRopeSettings::Get();
		const FName ChannelName = Settings ? Settings->EdenParticleChannelName : NAME_None;
		FMessageLog("PIE").Warning(
			FText::Format(
				LOCTEXT("EdenRopeChannelNotFound",
					"EdenRope: Collision channel '{0}' not found! Defaulting to PhysicsBody. "
					"Please configure a custom Object Channel named '{0}' in "
					"Project Settings > Collision > Object Channels."),
				FText::FromName(ChannelName)
			)
		);
	}
}

void FEdenRopeModule::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	// 当 EdenRopeSettings 或 CollisionProfile 属性变化时重新初始化
	if (Cast<UEdenRopeSettings>(Object) || Cast<UCollisionProfile>(Object))
	{
		Eden::InitializeEdenParticleChannel();
	}
}

#undef LOCTEXT_NAMESPACE
#endif

IMPLEMENT_MODULE(FEdenRopeModule, EdenRope)
