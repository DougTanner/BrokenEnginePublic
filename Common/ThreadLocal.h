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
	kThreadRender,
	kThreadSubmitGlobal,
	kThreadSubmitMain,
	kThreadPresent,
	kThreadScreenshot,

	kThreadCount
};

class ThreadLocal;
inline thread_local ThreadLocal* gpThreadLocal = nullptr;

// Note: "4096 - sizeof(DWORD)" is max length for OutputDebugString()
//       But with std:array iterators some Vulkan validation messages can overflow
inline constexpr int64_t kiLogBufferSize = 32 * 1024;

class ThreadLocal
{
public:

	ThreadLocal() = delete;
	ThreadLocal(std::array<char, kiLogBufferSize>& rLogBuffer, std::vector<std::byte>& rWorkbufferMemory, std::optional<int64_t> iThreadId = std::nullopt, bool bSetupExceptionHandling = true);
	~ThreadLocal();

	std::optional<int64_t> miThreadId;
	int64_t miLogIndent = 0;
	std::array<char, kiLogBufferSize>& mLogBuffer;
	Workbuffer mWorkbuffer;
};

} // namespace common
