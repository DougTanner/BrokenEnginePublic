#pragma once

#include <semaphore>
#include <thread>

#include "ThreadLocal.h"

namespace common
{

class PersistentWorker
{
public:

	PersistentWorker(Threads eThread, int64_t iWorkbufferSize = 0)
	: mThread([this, eThread, iWorkbufferSize]()
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
		ThreadLocal threadLocal(iWorkbufferSize, eThread);

		while (true)
		{
			mWake.acquire();
			if (mShutdown.load(std::memory_order_relaxed)) [[unlikely]]
			{
				break;
			}
			try
			{
				mWork();
			}
			catch (...)
			{
				mException = std::current_exception();
			}
			mDone.release();
		}
	})
	{
	}

	~PersistentWorker()
	{
		mShutdown.store(true, std::memory_order_relaxed);
		mWake.release();
		mThread.join();
	}

	void Wake(std::move_only_function<void()> work)
	{
		mWork = std::move(work);
		mbDispatched = true;
		mWake.release();
	}

	void Wait()
	{
		if (mbDispatched)
		{
			mDone.acquire();
			mbDispatched = false;

			if (mException != nullptr) [[unlikely]]
			{
				std::exception_ptr exception = std::exchange(mException, nullptr);
				std::rethrow_exception(exception);
			}
		}
	}

private:

	std::move_only_function<void()> mWork;
	std::binary_semaphore mWake {0};
	std::binary_semaphore mDone {0};
	std::atomic<bool> mShutdown {false};
	bool mbDispatched = false; // Only accessed by calling thread
	std::exception_ptr mException; // Only accessed between Wake/Wait synchronization points
	std::thread mThread; // Must be last (starts thread, needs other members initialized)
};

} // namespace common
