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
	: mWorkbufferMemory(iWorkbufferSize)
	, mThread([this, eThread]()
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
		ThreadLocal threadLocal(mLogBuffer, mWorkbufferMemory, eThread);

		while (true)
		{
			mWake.acquire();
			if (mShutdown.load(std::memory_order_relaxed)) [[unlikely]]
			{
				break;
			}
			mWork();
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
		}
	}

private:

	std::move_only_function<void()> mWork;
	std::binary_semaphore mWake {0};
	std::binary_semaphore mDone {0};
	std::atomic<bool> mShutdown {false};
	bool mbDispatched = false; // Only accessed by calling thread
	std::array<char, kiLogBufferSize> mLogBuffer {};
	std::vector<std::byte> mWorkbufferMemory;
	std::thread mThread; // Must be last (starts thread, needs other members initialized)
};

} // namespace common
