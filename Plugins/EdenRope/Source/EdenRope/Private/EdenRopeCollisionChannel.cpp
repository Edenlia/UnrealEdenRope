// Copyright Eden Games. All Rights Reserved.

#include "EdenRopeCollisionChannel.h"
#include "EdenRopeSettings.h"
#include "Engine/CollisionProfile.h"
#include "Macro/EdenRopeMacroDefine.h"

#define LOCTEXT_NAMESPACE "EdenRopeCollisionChannel"

namespace Eden
{
	ECollisionChannel GEdenParticleCollisionChannel = ECC_PhysicsBody;
	bool bEdenParticleChannelValid = false;

	void InitializeEdenParticleChannel()
	{
		const UEdenRopeSettings* Settings = UEdenRopeSettings::Get();
		if (!Settings)
		{
			UE_LOG(LogEdenRope, Warning, TEXT("InitializeEdenParticleChannel: UEdenRopeSettings not available"));
			return;
		}

		// 物理世界未启用时，碰撞通道不会被使用，静默默认为 PhysicsBody 且不发出警告
		if (!Settings->bEnableEdenPhysicsWorld)
		{
			GEdenParticleCollisionChannel = ECC_PhysicsBody;
			bEdenParticleChannelValid = false;
			return;
		}

		const FName ChannelName = Settings->EdenParticleChannelName;

		if (ChannelName.IsNone())
		{
			UE_LOG(LogEdenRope, Warning, TEXT("InitializeEdenParticleChannel: EdenParticleChannelName is not set, defaulting to PhysicsBody"));
			GEdenParticleCollisionChannel = ECC_PhysicsBody;
			bEdenParticleChannelValid = false;
			return;
		}

		UCollisionProfile* CollisionProfile = UCollisionProfile::Get();
		if (!CollisionProfile)
		{
			UE_LOG(LogEdenRope, Warning, TEXT("InitializeEdenParticleChannel: UCollisionProfile not available"));
			return;
		}

		// ReturnContainerIndexFromChannelName 没有 ENGINE_API 导出，无法从外部模块调用
		// 使用 ReturnChannelNameFromContainerIndex（有 ENGINE_API）反向查找
		bool bFound = false;
		for (int32 ChannelIndex = 0; ChannelIndex < ECC_MAX; ++ChannelIndex)
		{
			const FName CurrentChannelName = CollisionProfile->ReturnChannelNameFromContainerIndex(ChannelIndex);
			if (CurrentChannelName == ChannelName)
			{
				GEdenParticleCollisionChannel = static_cast<ECollisionChannel>(ChannelIndex);
				bEdenParticleChannelValid = true;
				bFound = true;
				UE_LOG(LogEdenRope, Log, TEXT("EdenParticle collision channel '%s' resolved to ECollisionChannel %d"),
					*ChannelName.ToString(), ChannelIndex);
				break;
			}
		}

		if (!bFound)
		{
			GEdenParticleCollisionChannel = ECC_PhysicsBody;
			bEdenParticleChannelValid = false;

			UE_LOG(LogEdenRope, Warning,
				TEXT("Collision channel '%s' not found, defaulting to PhysicsBody"),
				*ChannelName.ToString());
		}
	}
}

#undef LOCTEXT_NAMESPACE
