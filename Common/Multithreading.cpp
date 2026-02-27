#include "Multithreading.h"

namespace common
{

Multithreading::Multithreading(int64_t iWorkerCount)
{
	gpMultithreading = this;
	mWorkers.reserve(static_cast<size_t>(iWorkerCount));
	for (int64_t i = 0; i < iWorkerCount; ++i)
	{
		mWorkers.push_back(std::make_unique<PersistentWorker>(kThreadMultithreading, 65536));
	}
}

Multithreading::~Multithreading()
{
	gpMultithreading = nullptr;
}

} // namespace common
