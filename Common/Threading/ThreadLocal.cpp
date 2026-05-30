#include "ThreadLocal.h"

namespace common
{

ThreadLocal::ThreadLocal(int64_t iWorkbufferSize, std::optional<int64_t> iThreadId, bool bSetupExceptionHandling)
: miThreadId(iThreadId)
, mLogBufferMemory(kiLogBufferSize)
, mWorkbufferMemory(iWorkbufferSize)
, mpLogBuffer(mLogBufferMemory.data())
, mWorkbuffer(mWorkbufferMemory)
{
	ASSERT(gpThreadLocal == nullptr);

	gpThreadLocal = this;

	ConfigureThreadFloatingPoint();

	if (bSetupExceptionHandling)
	{
		SetupExceptionHandling();
	}
}

ThreadLocal::~ThreadLocal()
{
	gpThreadLocal = nullptr;
}

} // namespace common
