#pragma once

#include "LogTypes.h"
#include "LogFormatters.h"
#include "Threading/ThreadLocal.h"

namespace common
{

// Category names for crash dump labels
inline constexpr const char* kpcLogCategoryNames[] = {"Default", "Temp", "Audio", "Graphics", "Loading", "NavData", "Network", "Input"};
static_assert(std::size(kpcLogCategoryNames) == kiLogCategoryCount, "kpcLogCategoryNames out of sync with LogCategory");

// Mutable globals (defined in Log.cpp)
extern std::atomic<int64_t> giMyOutputDebugString;
extern std::mutex gLogMutex;

// Ring buffer: fixed-size lines, lockless write via atomic counter
template <int64_t LINE_COUNT, bool WRAP>
struct LogBuffer
{
	static constexpr int64_t kiLineCount = LINE_COUNT;

	std::atomic<int64_t> miWritePosition {0};
	char mLines[kiLineCount][kiLogBufferSize] {}; // last byte always 0

	char* AcquireLine()
	{
		int64_t iPosition = miWritePosition.fetch_add(1, std::memory_order_relaxed);
		if constexpr (!WRAP)
		{
			if (iPosition >= kiLineCount)
			{
				return nullptr;
			}
		}
		return mLines[iPosition % kiLineCount];
	}

	// Fills ppLines (caller capacity kiLineCount) with pointers to the most recent buffered lines in chronological
	// order (oldest returned line first, newest last); iMaxLines is the requested scan cap. Returns the number of
	// pointers written. Reuses AcquireLine's wrap math. Lockless/racy — a line being written concurrently may be torn
	// and empty slots are skipped (same tolerance as the crash-dump Dump path). Pass iMaxLines == kiLineCount to scan
	// the whole buffer.
	_Ret_range_(0, LINE_COUNT)
	int64_t Tail(_Out_writes_to_(LINE_COUNT, return) const char** ppLines, int64_t iMaxLines) const
	{
		int64_t iWritePos = miWritePosition.load(std::memory_order_relaxed);
		int64_t iAvailable = std::min(iWritePos, kiLineCount);
		int64_t iCount = std::min(iAvailable, iMaxLines);
		int64_t iFilled = 0;
		if constexpr (WRAP)
		{
			int64_t iFirst = iWritePos - iCount;
			for (int64_t i = 0; i < iCount; ++i)
			{
				const char* pLine = mLines[(iFirst + i) % kiLineCount];
				if (pLine[0] != '\0')
				{
					ppLines[iFilled++] = pLine;
				}
			}
		}
		else
		{
			int64_t iFirst = iAvailable - iCount;
			for (int64_t i = 0; i < iCount; ++i)
			{
				const char* pLine = mLines[iFirst + i];
				if (pLine[0] != '\0')
				{
					ppLines[iFilled++] = pLine;
				}
			}
		}
		return iFilled;
	}

	void Dump(std::ofstream& rOfstream) const
	{
		int64_t iWritePos = miWritePosition.load(std::memory_order_relaxed);
		if constexpr (WRAP)
		{
			int64_t iCount = std::min(iWritePos, kiLineCount);
			int64_t iStart = (iWritePos >= kiLineCount) ? (iWritePos % kiLineCount) : 0;
			for (int64_t i = 0; i < iCount; ++i)
			{
				const char* pLine = mLines[(iStart + i) % kiLineCount];
				if (pLine[0] != '\0')
				{
					rOfstream << pLine;
				}
			}
		}
		else
		{
			int64_t iCount = std::min(iWritePos, kiLineCount);
			for (int64_t i = 0; i < iCount; ++i)
			{
				if (mLines[i][0] != '\0')
				{
					rOfstream << mLines[i];
				}
			}
		}
	}
};

inline constexpr int64_t kiRingBufferLineCount = 128;
inline constexpr int64_t kiGlobalBufferLineCount = 1024;
inline constexpr int64_t kiAgentBufferLineCount = 512; // Wrapping cross-category ring for the agent get_logs default. Half of the global buffer's line count so the added static footprint stays bounded (kiAgentBufferLineCount * kiLogBufferSize = 16 MiB).

using LogRingBuffer = LogBuffer<kiRingBufferLineCount, true>;
using LogGlobalBuffer = LogBuffer<kiGlobalBufferLineCount, false>;
using LogAgentBuffer = LogBuffer<kiAgentBufferLineCount, true>;

extern LogRingBuffer gLogRingBuffers[kiLogCategoryCount];
extern LogGlobalBuffer gLogGlobalBuffer; // Non-wrapping; freezes after kiGlobalBufferLineCount lines — feeds the crash-dump path (do not repurpose).
extern LogAgentBuffer gLogAgentBuffer; // Wrapping; always holds the most recent lines across all categories for the agent get_logs default.

// Runtime per-category log threshold — checked by LOG() after compile-time filtering but before any argument
// formatting, so a suppressed line costs one relaxed atomic load plus a branch. Default kInfo (the documented
// out-of-box threshold); the agent lowers it live via set_log_level. Written from the main-thread agent Drain,
// read from every logging thread.
extern std::atomic<LogLevel> gLogRuntimeLevels[kiLogCategoryCount];
void SetLogRuntimeLevel(LogCategory eCategory, LogLevel eLevel);

// Whole-stream log-file sink — opened once from startup (--log-file). LogWrite tees each emitted line here under
// gLogMutex. Distinct from the selective per-file DiagnosticLog (FILE_LOG); the two coexist.
void EnableLogFile(const std::filesystem::path& rPath);

void LogIndent(int64_t iIndent);
char* LogPrefix(char* pLogBuffer, char* pEnd);
void LogWrite(char* pLogBuffer);
void LogWriteRingBuffers(const char* pLogBuffer, int64_t iLength, LogCategory eCategory);
void LogDumpBuffers(std::ofstream& rOfstream);

// Single per-thread fallback buffer for threads without a ThreadLocal (early startup; OS / driver / COM / gamepad
// threads). Hoisted out of the Log() template so there is exactly one kiLogBufferSize thread_local per thread rather
// than one per template instantiation. thread_local makes it race-free across threads, and a zero-init POD array is
// safe at the earliest startup (no dynamic init).
inline char* GetLogFallbackBuffer()
{
	static thread_local char spcLogBuffer[kiLogBufferSize] {};
	return spcLogBuffer;
}

// Unfiltered log writer — called by LOG() macro after compile-time filtering
template <typename... TUV>
void Log(LogCategory eCategory, std::format_string<const TUV&...> format, const TUV&... parameters)
{
	char* pLogBuffer = gpThreadLocal != nullptr ? gpThreadLocal->mpLogBuffer : GetLogFallbackBuffer();
	char* pEnd = pLogBuffer + kiLogBufferSize - 2; // Reserve 2 bytes for the trailing '\n' and '\0'.
	char* pWrite = LogPrefix(pLogBuffer, pEnd);

	if (eCategory == LogCategory::kTemp)
	{
		constexpr const char kpcTempPrefix[] = "kTemp: ";
		int64_t iTempLength = std::min(static_cast<int64_t>(sizeof(kpcTempPrefix) - 1), pEnd - pWrite);
		std::memcpy(pWrite, kpcTempPrefix, iTempLength);
		pWrite += iTempLength;
	}

	std::format_to_n_result<char*> formatResult = std::format_to_n(pWrite, pEnd - pWrite, format, parameters...);
	pWrite = formatResult.out;

	*(pWrite++) = '\n';
	*(pWrite++) = 0;

	LogWriteRingBuffers(pLogBuffer, pWrite - pLogBuffer, eCategory);
	LogWrite(pLogBuffer);
}

class ScopedLogIndent
{
public:

	ScopedLogIndent()
	{
		if constexpr (kbLogging)
		{
			LogIndent(1);
		}
	}

	~ScopedLogIndent()
	{
		if constexpr (kbLogging)
		{
			LogIndent(-1);
		}
	}

	ScopedLogIndent(const ScopedLogIndent&) = delete;
	ScopedLogIndent& operator=(const ScopedLogIndent&) = delete;
};

} // namespace common

using common::Log;
using common::LogIndent;
using common::ScopedLogIndent;

// Per-category compile-time thresholds (keLogLevel* defined in each project's Pch.h before Common.h)
inline constexpr LogLevel keLogLevels[]
{
	keLogLevelDefault, keLogLevelTemp,
	keLogLevelAudio, keLogLevelGraphics, keLogLevelLoading, keLogLevelNavData, keLogLevelNetwork, keLogLevelInput,
};
static_assert(std::size(keLogLevels) == common::kiLogCategoryCount, "keLogLevels out of sync with LogCategory");

// Compile-time log filtering: if constexpr eliminates filtered-out calls entirely
#define LOG(category, level, format, ...) \
do { \
	if constexpr (kbLogging && (level) >= keLogLevels[static_cast<int64_t>(category)]) \
	{ \
		if ((level) >= common::gLogRuntimeLevels[static_cast<int64_t>(category)].load(std::memory_order_relaxed)) \
		{ \
			common::Log((category), format __VA_OPT__(,) __VA_ARGS__); \
		} \
	} \
} while (false)
