#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
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
	std::atomic<bool> IsStarted{ false };
	std::mutex m_wait_mutex;
	std::condition_variable m_wait_cv;
};
