// Copyright Eden Games. All Rights Reserved.

#include "EdenPhysicsShowFlags.h"

// ========== Eden Physics ShowFlags ==========
// 在Editor的Show菜单 -> Visualize组下显示

TCustomShowFlag<> GShowEdenPhysicsParticles(
	TEXT("EdenPhysicsParticles"),
	false,  // 默认关闭
	SFG_Visualize,
	NSLOCTEXT("EdenPhysics", "EdenPhysicsParticlesSF", "Eden Physics Particles")
);

TCustomShowFlag<> GShowEdenMaterialFrameAxis(
	TEXT("EdenMaterialFrameAxis"),
	false,
	SFG_Visualize,
	NSLOCTEXT("EdenPhysics", "EdenMaterialFrameAxisSF", "Eden Material Frame Axis")
);

TCustomShowFlag<> GShowEdenPhysicsPinConstraint(
	TEXT("EdenPhysicsPinConstraint"),
	false,
	SFG_Visualize,
	NSLOCTEXT("EdenPhysics", "EdenPhysicsPinConstraintSF", "Eden Physics Pin Constraint")
);

TCustomShowFlag<> GShowEdenPhysicsDistanceConstraint(
	TEXT("EdenPhysicsDistanceConstraint"),
	false,
	SFG_Visualize,
	NSLOCTEXT("EdenPhysics", "EdenPhysicsDistanceConstraintSF", "Eden Physics Distance Constraint")
);

TCustomShowFlag<> GShowEdenPhysicsBendConstraint(
	TEXT("EdenPhysicsBendConstraint"),
	false,
	SFG_Visualize,
	NSLOCTEXT("EdenPhysics", "EdenPhysicsBendConstraintSF", "Eden Physics Bend Constraint")
);

TCustomShowFlag<> GShowEdenPhysicsCollisionConstraints(
	TEXT("EdenPhysicsCollisionConstraints"),
	false,
	SFG_Visualize,
	NSLOCTEXT("EdenPhysics", "EdenPhysicsCollisionConstraintsSF", "Eden Physics Collision Constraints")
);

TCustomShowFlag<> GShowEdenPhysicsRopeAABB(
	TEXT("EdenPhysicsRopeAABB"),
	false,
	SFG_Visualize,
	NSLOCTEXT("EdenPhysics", "EdenPhysicsRopeAABBSF", "Eden Physics Rope AABB")
);