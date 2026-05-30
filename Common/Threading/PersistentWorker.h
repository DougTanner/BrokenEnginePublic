#pragma once

#include "ThreadLocal.h"

namespace common
{

// Long-lived single-worker thread driven by a strict Wake/Wait ping-pong: exactly one Wake per Wait, Wait before the next Wake, Wait before destruction, all from a single calling thread. The worker lambda captures `this`, so the object outlives its thread and is non-copyable/non-movable.
class PersistentWorker
{
public:

	PersistentWorker(std::optional<int64_t> iThreadId, int64_t iWorkbufferSize = 0);
	~PersistentWorker();

	PersistentWorker(const PersistentWorker&) = delete; // Worker lambda captures `this`; deleting copy also suppresses the implicit move
	PersistentWorker& operator=(const PersistentWorker&) = delete;

	void Wake(std::move_only_function<void()> work);
	void Wait();

private:

	std::move_only_function<void()> mWork;
	std::binary_semaphore mWake {0};
	std::binary_semaphore mDone {0};
	std::atomic<bool> mShutdown {false}; // acquire/release names the shutdown signal's intent; the mWake/mDone semaphores already carry the happens-before edge, so read only immediately after mWake.acquire()
	bool mbDispatched = false; // Only accessed by calling thread
	std::exception_ptr mException; // Only accessed between Wake/Wait synchronization points
	std::thread mThread; // Must be last: the ctor lambda captures `this` and reads every other member, so declaration order is a correctness requirement
};

} // namespace common
