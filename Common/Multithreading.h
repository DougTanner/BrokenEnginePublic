#pragma once

#include "PersistentWorker.h"

namespace common
{

class Multithreading;
inline Multithreading* gpMultithreading = nullptr;

class Multithreading
{
public:

	explicit Multithreading(int64_t iWorkerCount);
	Multithreading(Threads eThread, int64_t iWorkerCount, int64_t iWorkbufferSize);
	~Multithreading();

	int64_t WorkerCount() const { return static_cast<int64_t>(mWorkers.size()); }
	bool IsMainThread() const { return std::this_thread::get_id() == mMainThreadId; }

	template <typename FUNC>
	void Dispatch(int64_t iCount, FUNC& processRange)
	{
		if (iCount <= 0)
		{
			return;
		}

		int64_t iThreadCount = static_cast<int64_t>(mWorkers.size()) + 1;
		int64_t iPerThread = iCount / iThreadCount;
		int64_t iRemainder = iCount % iThreadCount;

		int64_t iPos = 0;
		for (size_t i = 0; i < mWorkers.size(); ++i)
		{
			int64_t iThreadItems = iPerThread + (static_cast<int64_t>(i) < iRemainder ? 1 : 0);
			if (iThreadItems == 0)
			{
				break;
			}

			int64_t iStart = iPos;
			int64_t iEnd = iPos + iThreadItems;
			mWorkers[i]->Wake([&processRange, iStart, iEnd]()
			{
				processRange(iStart, iEnd);
			});
			iPos += iThreadItems;
		}

		// Main thread processes the remaining items
		if (iPos < iCount)
		{
			processRange(iPos, iCount);
		}

		// Wait for all workers (Wait is a no-op if not dispatched)
		for (std::unique_ptr<PersistentWorker>& pWorker : mWorkers)
		{
			pWorker->Wait();
		}
	}

private:

	std::thread::id mMainThreadId;
	std::vector<std::unique_ptr<PersistentWorker>> mWorkers;
};

} // namespace common
