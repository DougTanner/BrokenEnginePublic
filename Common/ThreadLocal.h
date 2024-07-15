#pragma once

namespace common
{

enum Threads
{
	kThreadNone,

	kThreadDataFile,
	kThreadTexturesFile,
	kThreadDxDiag,
};

class ThreadLocal;
inline thread_local ThreadLocal* gpThreadLocal = nullptr;

#if defined(BT_ENGINE)
void SetupExceptionHandling();
#endif

// Note: "4096 - sizeof(DWORD)" is max length for OutputDebugString()
//       But with std:array iterators some Vulkan validation messages can overflow
inline constexpr int64_t kiLogBufferSize = 32 * 1024;

class ThreadLocal
{
public:

	ThreadLocal(int64_t iWorkbufferInitialSize, std::optional<int64_t> iThreadId = std::nullopt)
	: miThreadId(iThreadId)
	{
		gpThreadLocal = this;

		mpLogBuffer = std::make_unique<std::array<char, kiLogBufferSize>>();

		mWorkbufferBytes.resize(iWorkbufferInitialSize);

	#if defined(BT_ENGINE)
		SetupExceptionHandling();
	#endif
	}

	~ThreadLocal()
	{
		gpThreadLocal = nullptr;
	}	

	ThreadLocal() = delete;

	template<typename T>
	T GetWorkbuffer(int64_t iSizeInBytes)
	{
		if (static_cast<int64_t>(mWorkbufferBytes.size()) < iSizeInBytes)
		{
			DEBUG_BREAK();
			mWorkbufferBytes.resize(iSizeInBytes);
		}

		return reinterpret_cast<T>(mWorkbufferBytes.data());
	}

	std::optional<int64_t> miThreadId;
	int64_t miLogIndent = 0;
	std::unique_ptr<std::array<char, kiLogBufferSize>> mpLogBuffer;

private:

	std::vector<std::byte> mWorkbufferBytes;
};

} // namespace common
