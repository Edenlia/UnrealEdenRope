// Fill out your copyright notice in the Description page of Project Settings.


#include "Utils/EpxGameplayStatics.h"

bool UEpxGameplayStatics::SetRopeAllParticleMass(UEdenRopeComponentBase* RopeBaseComponent, float PerParticleMass)
{
	if (!RopeBaseComponent || PerParticleMass <= 0.f)
	{
		return false;
	}

	RopeBaseComponent->SetMassPerParticle(PerParticleMass);
	return true;
}

bool UEpxGameplayStatics::SetRopeSingleParticleMass(UEdenRopeComponentBase* RopeBaseComponent, float PerParticleMass, int ParticleIndex)
{
	if (!RopeBaseComponent || PerParticleMass <= 0.f || ParticleIndex < 0)
	{
		return false;
	}

	RopeBaseComponent->SetSingleParticleMass(ParticleIndex, PerParticleMass);
	return true;
}
