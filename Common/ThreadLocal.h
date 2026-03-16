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
	kThreadSubmitGlobal,
	kThreadSubmitMain,
	kThreadPresent,
	kThreadScreenshot,
	kThreadMultithreading,
	kThreadReconcile,
	kThreadReconcileDispatch,

	kThreadCount
};

class ThreadLocal;
inline thread_local ThreadLocal* gpThreadLocal = nullptr;

// Note: "4096 - sizeof(DWORD)" is max length for OutputDebugString()
//       But some Vulkan validation messages can overflow that
inline constexpr int64_t kiLogBufferSize = 32 * 1024;

class ThreadLocal
{
public:

	ThreadLocal() = delete;
	ThreadLocal(int64_t iWorkbufferSize = 0, std::optional<int64_t> iThreadId = std::nullopt, bool bSetupExceptionHandling = true);
	~ThreadLocal();

	std::optional<int64_t> miThreadId;
	int64_t miLogIndent = 0;

private:

	std::vector<char> mLogBufferMemory;
	std::vector<std::byte> mWorkbufferMemory;

public:

	char* mpLogBuffer = nullptr;
	Workbuffer mWorkbuffer;
};

} // namespace common
