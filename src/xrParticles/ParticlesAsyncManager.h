#pragma once
#include "../xrGame/ParticlesObject.h"

class CPS_Instance;

class CParticlesAsync
{
public:
	static void Play();
	static void Wait();

	static void ForceUpdate(intrusive_ptr<CPS_Instance> Obj);
	static bool NeedForceUpdate();

private:
	void UpdateParticle(intrusive_ptr<CPS_Instance> particle) const;
	static void Start();

public:
	CParticlesAsync();

private:
	volatile bool IsStarted = false;
};
