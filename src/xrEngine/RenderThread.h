#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

extern ENGINE_API int ps_r__mt_decouple;

class ENGINE_API CRenderThread
{
public:
	CRenderThread() = default;
	~CRenderThread();

	CRenderThread(const CRenderThread&) = delete;
	CRenderThread& operator=(const CRenderThread&) = delete;

	void Start();
	void Stop();

	// Enqueue one complete render frame task onto the dedicated render thread.
	// Caller must have already waited for the previous frame before calling Submit.
	void Submit(std::function<void()>&& work);

	// Block until any in-flight render frame has completed.
	void Wait();

	bool IsRunning() const noexcept { return m_running.load(std::memory_order_acquire); }
	bool IsBusy() const noexcept
	{
		return m_completed_gen.load(std::memory_order_acquire) != m_submitted_gen.load(std::memory_order_acquire);
	}

	static bool IsOnRenderThread() noexcept;

	float GetLastWaitMs() const noexcept { return m_last_wait_ms.load(std::memory_order_relaxed); }
	float GetLastRenderMs() const noexcept { return m_last_render_ms.load(std::memory_order_relaxed); }

private:
	void ThreadMain();

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv_work;
	std::condition_variable m_cv_done;

	std::function<void()> m_pending_work;
	std::atomic<u64> m_submitted_gen{ 0 };
	std::atomic<u64> m_completed_gen{ 0 };
	std::atomic<bool> m_running{ false };
	bool m_exit{ false };

	std::atomic<float> m_last_wait_ms{ 0.f };
	std::atomic<float> m_last_render_ms{ 0.f };
};
