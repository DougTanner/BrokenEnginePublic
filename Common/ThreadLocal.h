#pragma once

#include "Workbuffer.h"

namespace common
{

enum Threads
{
	kThreadNone,

	kThreadEagerLoad,
	kThreadLazyLoad,
	kThreadTextureUpload,
	kThreadDxDiag,

	kThreadCount
};

class ThreadLocal;
inline thread_local ThreadLocal* gpThreadLocal = nullptr;

void SetupExceptionHandling();

// Note: "4096 - sizeof(DWORD)" is max length for OutputDebugString()
//       But with std:array iterators some Vulkan validation messages can overflow
inline constexpr int64_t kiLogBufferSize = 32 * 1024;

class ThreadLocal
{
public:

	ThreadLocal(int64_t iWorkbufferInitialSize, std::optional<int64_t> iThreadId = std::nullopt, bool bSetupExceptionHandling = true)
	: miThreadId(iThreadId)
	, mWorkbuffer(iWorkbufferInitialSize)
	{
		gpThreadLocal = this;

		mpLogBuffer = std::make_unique<std::array<char, kiLogBufferSize>>();

		if (bSetupExceptionHandling)
		{
			SetupExceptionHandling();
		}
	}

	~ThreadLocal()
	{
		gpThreadLocal = nullptr;
	}

	ThreadLocal() = delete;

	std::optional<int64_t> miThreadId;
	int64_t miLogIndent = 0;
	std::unique_ptr<std::array<char, kiLogBufferSize>> mpLogBuffer;
	Workbuffer mWorkbuffer;
};

} // namespace common
