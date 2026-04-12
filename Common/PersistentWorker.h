#pragma once

#include "ThreadLocal.h"

namespace common
{

class PersistentWorker
{
public:

	PersistentWorker(std::optional<int64_t> iThreadId, int64_t iWorkbufferSize = 0);
	~PersistentWorker();

	void Wake(std::move_only_function<void()> work);
	void Wait();

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
