#include "stdafx.h"
#include "ParticlesAsyncManager.h"

#include "../Include/xrRender/ParticleCustom.h"
#include "../Include/xrRender/RenderVisual.h"
#include "../xrEngine/IGame_Persistent.h"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

static CParticlesAsync Instance;

void CParticlesAsync::Play()
{
	if (DevicePtr && !Device.ParticleWorkerCallback)
		Device.ParticleWorkerCallback.bind(&CParticlesAsync::Start);

	if (!psDeviceFlags.test(mtParticles))
	{
		Instance.Start();
	}
}

void CParticlesAsync::Start()
{
	Instance.IsStarted.store(true, std::memory_order_release);

	auto& active = g_pGamePersistent->ps_active;
	const size_t count = active.size();

	{
		PROF_EVENT("Particle Update", PROF_CAT(VFX));
		if (count >= 16)
		{
			tbb::parallel_for(tbb::blocked_range<size_t>(0, count, 8),
				[&](const tbb::blocked_range<size_t>& r)
				{
					for (size_t i = r.begin(); i < r.end(); ++i)
					{
						if (!active[i]->m_bDead)
							Instance.UpdateParticle(active[i]);
					}
				});
		}
		else
		{
			for (size_t i = 0; i < count; ++i)
			{
				if (!active[i]->m_bDead)
					Instance.UpdateParticle(active[i]);
			}
		}
	}

	{
		PROF_EVENT("Particle Shedule", PROF_CAT(VFX));
		for (intrusive_ptr<CPS_Instance>& particle : active)
			particle->Update(Device.dwTimeDelta);
	}

	{
		std::lock_guard<std::mutex> lock(Instance.m_wait_mutex);
		Instance.IsStarted.store(false, std::memory_order_release);
	}
	Instance.m_wait_cv.notify_all();
}

void CParticlesAsync::Wait()
{
	if (!psDeviceFlags.test(mtParticles))
		return;

	if (!Instance.IsStarted.load(std::memory_order_acquire))
		return;

	PROF_EVENT("Particles Wait", PROF_CAT(Wait));
	std::unique_lock<std::mutex> lock(Instance.m_wait_mutex);
	Instance.m_wait_cv.wait(lock, []()
	{
		return !Instance.IsStarted.load(std::memory_order_acquire);
	});
}

void CParticlesAsync::ForceUpdate(intrusive_ptr<CPS_Instance> Obj)
{
	if (!Instance.IsStarted.load(std::memory_order_acquire))
		return;

	Instance.UpdateParticle(Obj);
}

bool CParticlesAsync::NeedForceUpdate()
{
	return Instance.IsStarted.load(std::memory_order_acquire);
}

CParticlesAsync::CParticlesAsync()
{
	if (!DevicePtr)
		return;

	if (g_dedicated_server)
		return;

    Device.ParticleWorkerCallback.bind(&CParticlesAsync::Start);
}

void CParticlesAsync::UpdateParticle(intrusive_ptr<CPS_Instance> particle) const
{
	u32 dt = Device.dwTimeGlobal - particle->dwLastTime;
	IParticleCustom* V = smart_cast<IParticleCustom*>(particle->renderable.visual);

	if (V == nullptr)
		return;

	PROF_EVENT("Particle OnFrame", PROF_CAT(VFX));
	V->OnFrame(dt);

	particle->dwLastTime = Device.dwTimeGlobal;
}
