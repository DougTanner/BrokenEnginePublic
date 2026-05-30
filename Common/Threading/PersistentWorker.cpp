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
		// Read only right after mWake.acquire(): this acquire-load pairs with the destructor's release-store (the mWake/mDone semaphores already carry the happens-before edge)
		if (mShutdown.load(std::memory_order_acquire)) [[unlikely]]
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
	ASSERT(!mbDispatched); // Destruction requires an idle worker (Wait before destroy); otherwise this release() over-releases mWake or the worker consumes the token as work and join() hangs
	mShutdown.store(true, std::memory_order_release);
	mWake.release();
	mThread.join();
}

void PersistentWorker::Wake(std::move_only_function<void()> work)
{
	ASSERT(!mbDispatched); // Single-dispatch: a second Wake before Wait would re-release the count-1 mWake (past max() == 1, UB) and clobber the in-flight mWork
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
