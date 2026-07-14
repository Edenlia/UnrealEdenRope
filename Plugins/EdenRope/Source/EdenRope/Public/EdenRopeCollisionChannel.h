// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

namespace Eden
{
	/**
	 * 全局缓存的 EdenParticle 碰撞通道
	 * 在模块启动时从 UEdenRopeSettings::EdenParticleChannelName 动态查找
	 * 如果未找到对应通道，默认为 ECC_PhysicsBody
	 */
	extern EDENROPE_API ECollisionChannel GEdenParticleCollisionChannel;

	/**
	 * 通道是否有效（是否成功从配置中查找到）
	 * 如果为 false，表示使用的是回退值 ECC_PhysicsBody
	 */
	extern EDENROPE_API bool bEdenParticleChannelValid;

	/**
	 * 初始化 EdenParticle 碰撞通道
	 * 从 UEdenRopeSettings 读取通道名称，使用 UCollisionProfile 进行反查
	 * 应在 FEdenRopeModule::StartupModule() 中调用
	 */
	EDENROPE_API void InitializeEdenParticleChannel();
}
