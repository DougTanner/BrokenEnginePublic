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
	kThreadStreamingVoiceFill,
	kThreadMultithreading,

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
	int64_t miLogTickCounter = -1;

private:

	std::vector<char> mLogBufferMemory;
	std::vector<std::byte> mWorkbufferMemory;

public:

	char* mpLogBuffer = nullptr;
	Workbuffer mWorkbuffer;
};

class LogTickScope
{
public:

	explicit LogTickScope(int64_t iTick)
	{
		miPrior = gpThreadLocal->miLogTickCounter;
		gpThreadLocal->miLogTickCounter = iTick;
	}

	~LogTickScope()
	{
		gpThreadLocal->miLogTickCounter = miPrior;
	}

	LogTickScope(const LogTickScope&) = delete;
	LogTickScope& operator=(const LogTickScope&) = delete;

private:

	int64_t miPrior = -1;
};

} // namespace common
