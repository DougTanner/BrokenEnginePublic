#include "Log.h"

#include "AllocationTracking.h"

namespace common
{

std::atomic<int64_t> giMyOutputDebugString = 0;
std::mutex gLogMutex;

LogRingBuffer gLogRingBuffers[kiLogCategoryCount];
LogGlobalBuffer gLogGlobalBuffer;
LogAgentBuffer gLogAgentBuffer;

#if defined(BT_DATA_PACKER)
constexpr LogLevel keLogRuntimeDefault = kVerbose; // Offline tool: no agent / set_log_level, keep full verbosity (unchanged behavior)
#else
constexpr LogLevel keLogRuntimeDefault = kInfo; // Documented out-of-box threshold; the agent lowers it live via set_log_level
#endif
std::atomic<LogLevel> gLogRuntimeLevels[kiLogCategoryCount]
{
	keLogRuntimeDefault, kVerbose, // [1] kTemp: transient agent/dev diagnostics — always emit (compile floor keLogLevelTemp = kVerbose)
	keLogRuntimeDefault, keLogRuntimeDefault, keLogRuntimeDefault, keLogRuntimeDefault, keLogRuntimeDefault, keLogRuntimeDefault,
};
static_assert(std::size(gLogRuntimeLevels) == kiLogCategoryCount, "gLogRuntimeLevels out of sync with LogCategory");

// Whole-stream file sink (Log.h EnableLogFile); teed by LogWrite under gLogMutex.
static std::ofstream sLogFileStream;

void SetLogRuntimeLevel(LogCategory eCategory, LogLevel eLevel)
{
	gLogRuntimeLevels[static_cast<int64_t>(eCategory)].store(eLevel, std::memory_order_relaxed);
}

void EnableLogFile(const std::filesystem::path& rPath)
{
	bool bOpened = false;
	{
		std::unique_lock lockGuard(gLogMutex);
		sLogFileStream.open(rPath, std::ios::out | std::ios::trunc);
		bOpened = sLogFileStream.is_open();
	}

	// LOG tees through LogWrite, which locks gLogMutex — must log only after the lock above releases (gLogMutex is
	// non-recursive). Runs at startup before allocation tracking arms, so rPath.string() allocating is acceptable.
	if (!bOpened)
	{
		LOG(kDefault, kWarning, "EnableLogFile failed to open log file: {}", rPath.string());
	}
}

void LogIndent(int64_t iIndent)
{
	if constexpr (kbLogging)
	{
		if (gpThreadLocal != nullptr)
		{
			gpThreadLocal->miLogIndent += iIndent;
		}
	}
}

char* LogPrefix(char* pLogBuffer, char* pEnd)
{
	char* pWrite = pLogBuffer;

	auto put = [&pWrite, pEnd](char c)
	{
		if (pWrite < pEnd)
		{
			*(pWrite++) = c;
		}
	};

	if (gpThreadLocal != nullptr) [[likely]]
	{
		int64_t iLogIndent = gpThreadLocal->miLogIndent;
		for (int64_t i = 0; i < iLogIndent; ++i)
		{
			put(' ');
			put(' ');
		}

		if (gpThreadLocal->miThreadId.has_value())
		{
			std::to_chars_result threadIdResult = std::to_chars(pWrite, pEnd, gpThreadLocal->miThreadId.value());
			pWrite = threadIdResult.ptr;

			put(':');
			put(' ');
		}

		if (gpThreadLocal->miLogTickCounter >= 0)
		{
			put('[');
			put('T');
			put('i');
			put('c');
			put('k');
			put(':');
			put(' ');
			std::to_chars_result toCharsResult = std::to_chars(pWrite, pEnd, gpThreadLocal->miLogTickCounter);
			pWrite = toCharsResult.ptr;
			put(']');
			put(' ');
		}
	}
	else
	{
		put('#');
		put(':');
		put(' ');
	}

	return pWrite;
}

void LogWrite(char* pLogBuffer)
{
	std::unique_lock lockGuard(gLogMutex);
	++giMyOutputDebugString;
	OutputDebugString(pLogBuffer);
	--giMyOutputDebugString;
	if constexpr (kbAlsoLogToPrintf)
	{
		std::printf("%s", pLogBuffer);
	}
	if (sLogFileStream.is_open())
	{
		ScopedSuppressAllocationTracking suppress; // Heap: ofstream write/flush may allocate post-open; off the tracked main-loop path
		sLogFileStream << pLogBuffer;
		sLogFileStream.flush();
	}
}

void LogWriteRingBuffers(const char* pLogBuffer, int64_t iLength, LogCategory eCategory)
{
	auto copyToLine = [pLogBuffer, iLength](char* pLine)
	{
		if (pLine == nullptr)
		{
			return;
		}
		std::memcpy(pLine, pLogBuffer, iLength);
	};

	copyToLine(gLogRingBuffers[static_cast<int64_t>(eCategory)].AcquireLine());
	copyToLine(gLogGlobalBuffer.AcquireLine());
	copyToLine(gLogAgentBuffer.AcquireLine());
}

void LogDumpBuffers(std::ofstream& rOfstream)
{
	rOfstream << "\n\n\n<Begin Global Log>\n" << std::flush;
	gLogGlobalBuffer.Dump(rOfstream);
	rOfstream << "<End Global Log>\n" << std::flush;

	for (int64_t i = 0; i < kiLogCategoryCount; ++i)
	{
		rOfstream << "\n\n\n<Begin " << kpcLogCategoryNames[i] << " Log>\n" << std::flush;
		gLogRingBuffers[i].Dump(rOfstream);
		rOfstream << "<End " << kpcLogCategoryNames[i] << " Log>\n" << std::flush;
	}
}

} // namespace common
