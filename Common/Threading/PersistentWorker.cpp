#include "PersistentWorker.h"

namespace common
{

PersistentWorker::PersistentWorker(std::optional<int64_t> iThreadId, int64_t iWorkbufferSize)
: mThread([this, iThreadId, iWorkbufferSize]()
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	ThreadLocal threadLocal(iWorkbufferSize, iThreadId);

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

PersistentWorker::~PersistentWorker()
{
	mShutdown.store(true, std::memory_order_relaxed);
	mWake.release();
	mThread.join();
}

void PersistentWorker::Wake(std::move_only_function<void()> work)
{
	mWork = std::move(work);
	mbDispatched = true;
	mWake.release();
}

void PersistentWorker::Wait()
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

} // namespace common
