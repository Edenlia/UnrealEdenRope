// Copyright Eden Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShowFlags.h"

/**
 * Eden Physics ShowFlags
 * 在Editor的Show菜单 -> Visualize组下显示
 */

// 显示粒子位置
extern EDENROPE_API TCustomShowFlag<> GShowEdenPhysicsParticles;

// 显示 Rod Material Frame 坐标轴（仅 Rod 的 edge 中点，X=红 Y=绿 Z=蓝）
extern EDENROPE_API TCustomShowFlag<> GShowEdenMaterialFrameAxis;

// 显示 Pin 约束
extern EDENROPE_API TCustomShowFlag<> GShowEdenPhysicsPinConstraint;

// 显示距离约束
extern EDENROPE_API TCustomShowFlag<> GShowEdenPhysicsDistanceConstraint;

// 显示弯曲约束
extern EDENROPE_API TCustomShowFlag<> GShowEdenPhysicsBendConstraint;

// 显示碰撞约束
extern EDENROPE_API TCustomShowFlag<> GShowEdenPhysicsCollisionConstraints;

// 显示绳索 AABB
extern EDENROPE_API TCustomShowFlag<> GShowEdenPhysicsRopeAABB;
