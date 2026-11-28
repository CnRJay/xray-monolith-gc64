#include "stdafx.h"
#include "RenderThread.h"
#include "../xrCore/profiler.h"
#include <objbase.h>

ENGINE_API int ps_r__mt_decouple = 1;

static thread_local bool s_is_render_thread = false;

CRenderThread::~CRenderThread()
{
	Stop();
}

bool CRenderThread::IsOnRenderThread() noexcept
{
	return s_is_render_thread;
}

void CRenderThread::Start()
{
	if (m_running.load(std::memory_order_acquire))
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_exit = false;
		m_pending_work = nullptr;
	}

	m_running.store(true, std::memory_order_release);
	m_thread = std::thread(&CRenderThread::ThreadMain, this);
}

void CRenderThread::Stop()
{
	if (!m_running.load(std::memory_order_acquire))
		return;

	Wait();

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_exit = true;
	}
	m_cv_work.notify_one();

	if (m_thread.joinable())
		m_thread.join();

	m_running.store(false, std::memory_order_release);
}

void CRenderThread::Submit(std::function<void()>&& work)
{
	if (!m_running.load(std::memory_order_acquire))
	{
		if (work)
			work();
		return;
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		R_ASSERT2(!m_pending_work, "CRenderThread::Submit called while previous work was not waited");
		m_pending_work = std::move(work);
		m_submitted_gen.fetch_add(1, std::memory_order_release);
	}
	m_cv_work.notify_one();
}

void CRenderThread::Wait()
{
	if (!m_running.load(std::memory_order_acquire))
		return;

	const u64 target = m_submitted_gen.load(std::memory_order_acquire);
	if (m_completed_gen.load(std::memory_order_acquire) == target)
	{
		m_last_wait_ms.store(0.f, std::memory_order_relaxed);
		return;
	}

	PROF_EVENT("RT: Wait Render Thread", PROF_CAT(Wait));
	CTimer wait_timer;
	wait_timer.Start();

	std::unique_lock<std::mutex> lock(m_mutex);
	m_cv_done.wait(lock, [this, target]()
	{
		return m_completed_gen.load(std::memory_order_acquire) == target;
	});

	m_last_wait_ms.store(wait_timer.GetElapsed_ms_f(), std::memory_order_relaxed);
}

void CRenderThread::ThreadMain()
{
	thread_name("X-RAY Render thread");
	PROF_THREAD("Render Thread");
	s_is_render_thread = true;

	const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	while (true)
	{
		std::function<void()> task;
		u64 gen = 0;

		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv_work.wait(lock, [this]()
			{
				return m_exit || static_cast<bool>(m_pending_work);
			});

			if (m_exit && !m_pending_work)
				break;

			task = std::move(m_pending_work);
			gen = m_submitted_gen.load(std::memory_order_acquire);
		}

		if (task)
		{
			PROF_EVENT("RT: Execute Frame", PROF_CAT(Rendering));
			CTimer rt_timer;
			rt_timer.Start();

			task();

			m_last_render_ms.store(rt_timer.GetElapsed_ms_f(), std::memory_order_relaxed);
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_completed_gen.store(gen, std::memory_order_release);
		}
		m_cv_done.notify_all();
	}

	if (SUCCEEDED(com_hr))
		CoUninitialize();

	s_is_render_thread = false;
}
