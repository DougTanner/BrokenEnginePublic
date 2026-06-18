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

// Exactly one ThreadLocal per thread; it owns gpThreadLocal for its lifetime.
class ThreadLocal
{
public:

	ThreadLocal() = delete;
	ThreadLocal(int64_t iWorkbufferSize = 0, std::optional<int64_t> iThreadId = std::nullopt, bool bSetupExceptionHandling = true);
	~ThreadLocal();

	// Non-copyable/non-movable: mpLogBuffer/mWorkbuffer alias this object's own backing vectors.
	ThreadLocal(const ThreadLocal&) = delete;
	ThreadLocal& operator=(const ThreadLocal&) = delete;
	ThreadLocal(ThreadLocal&&) = delete;
	ThreadLocal& operator=(ThreadLocal&&) = delete;

	std::optional<int64_t> miThreadId;
	int64_t miLogIndent = 0;
	int64_t miLogTickCounter = -1;
	// True while this thread is executing a deterministic frame tick (set by FrameTickScope at the top
	// of RunFrameTick). Read to enforce the IslandTerrain Frame Purity Constraint.
	bool mbInFrameTick = false;

private:

	// Must precede mpLogBuffer/mWorkbuffer below: those alias this storage (ctor member-init order depends on it).
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
		ASSERT(gpThreadLocal != nullptr);
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

// Marks the calling thread as inside a deterministic frame tick for its scope (instantiated at the top
// of RunFrameTick, which runs on the per-coord dispatch workers and the client reconcile-replay thread).
// IslandTerrain::GlobalElevation reads the flag to fail fast if the render-only terrain query is ever
// called from frame-tick code (the Frame Purity Constraint). Saves/restores the prior value for nesting
// safety, mirroring LogTickScope.
class FrameTickScope
{
public:

	FrameTickScope()
	{
		ASSERT(gpThreadLocal != nullptr);
		mbPrior = gpThreadLocal->mbInFrameTick;
		gpThreadLocal->mbInFrameTick = true;
	}

	~FrameTickScope()
	{
		gpThreadLocal->mbInFrameTick = mbPrior;
	}

	FrameTickScope(const FrameTickScope&) = delete;
	FrameTickScope& operator=(const FrameTickScope&) = delete;

private:

	bool mbPrior = false;
};

// Sets miLogIndent to an absolute value and restores the prior on exit (cross-thread indent propagation); for relative +1/-1 indentation use ScopedLogIndent (Log.h)
class LogIndentScope
{
public:

	explicit LogIndentScope(int64_t iIndent)
	{
		ASSERT(gpThreadLocal != nullptr);
		miPrior = gpThreadLocal->miLogIndent;
		gpThreadLocal->miLogIndent = iIndent;
	}

	~LogIndentScope()
	{
		gpThreadLocal->miLogIndent = miPrior;
	}

	LogIndentScope(const LogIndentScope&) = delete;
	LogIndentScope& operator=(const LogIndentScope&) = delete;

private:

	int64_t miPrior = 0;
};

} // namespace common
