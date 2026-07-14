// Copyright Eden Games. All Rights Reserved.

#include "RigidBodyProxy/RigidBodyProxy.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"

namespace Eden
{

void FDynamicRigidbodyProxy::UpdateFromUnreal()
{
	if (!OwnerComponent.IsValid())
	{
		return;
	}

	UPrimitiveComponent* Comp = OwnerComponent.Get();
	FBodyInstance* BodyInstance = Comp->GetBodyInstance();
	if (!BodyInstance)
	{
		return;
	}

	// 检查是否是 Kinematic
	bIsKinematic = BodyInstance->IsInstanceSimulatingPhysics() == false;

	if (bIsKinematic)
	{
		// Kinematic 物体：质量无限大，不受力影响
		InverseMass = 0.0f;
		InverseInertiaTensor = FMatrix::Identity * 0.0f;  // 零矩阵
	}
	else
	{
		// Dynamic 物体：计算逆质量和逆惯性张量
		const float Mass = BodyInstance->GetBodyMass();
		InverseMass = (Mass > KINDA_SMALL_NUMBER) ? (1.0f / Mass) : 0.0f;

		// 获取惯性张量（局部空间，对角线形式）
		// UE 的 InertiaTensor 是以 kg·cm² 为单位，需要转换为 kg·m²
		const FVector InertiaTensorLocal = BodyInstance->GetBodyInertiaTensor();
		// cm² → m²: 除以 10000
		const FVector InertiaTensorLocalM = InertiaTensorLocal * 0.0001f;

		// 计算逆惯性张量（对角线）
		FVector InverseInertiaDiag;
		InverseInertiaDiag.X = (InertiaTensorLocalM.X > KINDA_SMALL_NUMBER) ? (1.0f / InertiaTensorLocalM.X) : 0.0f;
		InverseInertiaDiag.Y = (InertiaTensorLocalM.Y > KINDA_SMALL_NUMBER) ? (1.0f / InertiaTensorLocalM.Y) : 0.0f;
		InverseInertiaDiag.Z = (InertiaTensorLocalM.Z > KINDA_SMALL_NUMBER) ? (1.0f / InertiaTensorLocalM.Z) : 0.0f;

		// 获取旋转矩阵（世界空间）
		const FQuat BodyRotation = BodyInstance->GetUnrealWorldTransform().GetRotation();
		const FMatrix RotationMatrix = FQuatRotationMatrix(BodyRotation);
		const FMatrix RotationMatrixT = RotationMatrix.GetTransposed();

		// 构建对角矩阵
		FMatrix InverseInertiaDiagMatrix = FMatrix::Identity;
		InverseInertiaDiagMatrix.M[0][0] = InverseInertiaDiag.X;
		InverseInertiaDiagMatrix.M[1][1] = InverseInertiaDiag.Y;
		InverseInertiaDiagMatrix.M[2][2] = InverseInertiaDiag.Z;
		InverseInertiaDiagMatrix.M[3][3] = 1.0f;

		// 世界空间的逆惯性张量: R * I⁻¹_local * Rᵀ
		InverseInertiaTensor = RotationMatrix * InverseInertiaDiagMatrix * RotationMatrixT;
	}

	// 获取质心位置（UE 单位 cm → m）
	CenterOfMass = BodyInstance->GetCOMPosition() * 0.01f;

	// 获取组件 pivot 位置（UE 单位 cm → m）
	PivotPosition = Comp->GetComponentLocation() * 0.01f;

	// 获取旋转
	Rotation = BodyInstance->GetUnrealWorldTransform().GetRotation();

	// 获取线速度（UE 单位 cm/s → m/s）
	Velocity = BodyInstance->GetUnrealWorldVelocity() * 0.01f;

	// 获取角速度（已经是 rad/s）
	AngularVelocity = BodyInstance->GetUnrealWorldAngularVelocityInRadians();
}

void FDynamicRigidbodyProxy::ApplyDeltasToUnreal()
{
	if (!OwnerComponent.IsValid() || bIsKinematic)
	{
		return;
	}

	// 检查是否有显著的速度变化
	if (DeltaLinearVelocity.SizeSquared() < KINDA_SMALL_NUMBER &&
		DeltaAngularVelocity.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		return;
	}

	UPrimitiveComponent* Comp = OwnerComponent.Get();
	FBodyInstance* BodyInstance = Comp->GetBodyInstance();
	if (!BodyInstance || !BodyInstance->IsInstanceSimulatingPhysics())
	{
		return;
	}

	// 获取当前速度（m/s → cm/s）
	const FVector CurrentLinearVelocity = BodyInstance->GetUnrealWorldVelocity();
	const FVector CurrentAngularVelocity = BodyInstance->GetUnrealWorldAngularVelocityInRadians();

	// 应用速度增量（m/s → cm/s）
	const FVector NewLinearVelocity = CurrentLinearVelocity + DeltaLinearVelocity * 100.0f;
	const FVector NewAngularVelocity = CurrentAngularVelocity + DeltaAngularVelocity;

	// 设置新速度
	BodyInstance->SetLinearVelocity(NewLinearVelocity, false);
	BodyInstance->SetAngularVelocityInRadians(NewAngularVelocity, false);
}

float FDynamicRigidbodyProxy::GetEffectiveInverseMass(const FVector& Direction, const FVector& WorldPoint) const
{
	if (bIsKinematic)
	{
		return 0.0f;  // Kinematic 物体有无限质量
	}

	// 线性部分
	float LinearW = InverseMass;

	// 角度部分: (r × n)ᵀ · I⁻¹ · (r × n)
	const FVector r = WorldPoint - CenterOfMass;
	const FVector AngularAxis = FVector::CrossProduct(r, Direction);
	
	// I⁻¹ · (r × n)
	const FVector IInvTimesAxis = InverseInertiaTensor.TransformVector(AngularAxis);
	
	// (r × n)ᵀ · I⁻¹ · (r × n)
	const float AngularW = FVector::DotProduct(AngularAxis, IInvTimesAxis);

	return LinearW + AngularW;
}

void FDynamicRigidbodyProxy::AccumulateImpulse(const FVector& Impulse, const FVector& WorldPoint)
{
	if (bIsKinematic)
	{
		return;  // Kinematic 物体不受力影响
	}

	// 线速度变化: Δv = J / m
	DeltaLinearVelocity += InverseMass * Impulse;

	// 角速度变化: Δω = I⁻¹ · (r × J)
	const FVector r = WorldPoint - CenterOfMass;
	const FVector Torque = FVector::CrossProduct(r, Impulse);
	DeltaAngularVelocity += InverseInertiaTensor.TransformVector(Torque);
}

} // namespace Eden
