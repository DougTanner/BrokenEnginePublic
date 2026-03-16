#include "Multithreading.h"

namespace common
{

Multithreading::Multithreading(int64_t iWorkerCount)
: mMainThreadId(std::this_thread::get_id())
{
	gpMultithreading = this;
	mWorkers.reserve(static_cast<size_t>(iWorkerCount));
	for (int64_t i = 0; i < iWorkerCount; ++i)
	{
		mWorkers.push_back(std::make_unique<PersistentWorker>(kThreadMultithreading, 65536));
	}
}

Multithreading::Multithreading(Threads eThread, int64_t iWorkerCount, int64_t iWorkbufferSize)
: mMainThreadId(std::this_thread::get_id())
{
	mWorkers.reserve(static_cast<size_t>(iWorkerCount));
	for (int64_t i = 0; i < iWorkerCount; ++i)
	{
		mWorkers.push_back(std::make_unique<PersistentWorker>(eThread, iWorkbufferSize));
	}
}

Multithreading::~Multithreading()
{
	if (gpMultithreading == this)
	{
		gpMultithreading = nullptr;
	}
}

} // namespace common
