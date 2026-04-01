#pragma once

#include "LogFormatters.h"
#include "ThreadLocal.h"

namespace common
{

// Log levels (priority hierarchy: Verbose < Debug < Warning < Error)
enum class LogLevel : int8_t
{
	kVerbose = 0,
	kDebug   = 1,
	kWarning = 2,
	kError   = 3,
};

// Log categories
enum class LogCategory : int8_t
{
	kAudio    = 0,
	kDefault  = 1,
	kGraphics = 2,
	kLoading  = 3,
	kNetwork  = 4,
};
inline constexpr int64_t kiLogCategoryCount = 5;

// Category names for crash dump labels
inline constexpr const char* kpcLogCategoryNames[] = {"Audio", "Default", "Graphics", "Loading", "Network"};

// Mutable globals (defined in Log.cpp)
extern LogLevel geLogLevel;
extern LogCategory geFocusedLogCategory;
extern std::atomic<int64_t> giMyOutputDebugString;
extern std::mutex gLogMutex;

// Ring buffer: fixed-size lines, lockless write via atomic counter
template <int64_t LINE_COUNT, bool WRAP>
struct LogBuffer
{
	static constexpr int64_t kiLineCount = LINE_COUNT;

	std::atomic<int64_t> miWritePosition {0};
	char mLines[kiLineCount][kiLogBufferSize]; // last byte always 0

	char* AcquireLine()
	{
		int64_t iPosition = miWritePosition.fetch_add(1, std::memory_order_relaxed);
		if constexpr (!WRAP)
		{
			if (iPosition >= kiLineCount)
				return nullptr;
		}
		return mLines[iPosition % kiLineCount];
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
					rOfstream << pLine;
			}
		}
		else
		{
			int64_t iCount = std::min(iWritePos, kiLineCount);
			for (int64_t i = 0; i < iCount; ++i)
			{
				if (mLines[i][0] != '\0')
					rOfstream << mLines[i];
			}
		}
	}
};

inline constexpr int64_t kiRingBufferLineCount = 128;
inline constexpr int64_t kiGlobalBufferLineCount = 1024;

using LogRingBuffer = LogBuffer<kiRingBufferLineCount, true>;
using LogGlobalBuffer = LogBuffer<kiGlobalBufferLineCount, false>;

extern LogRingBuffer gLogRingBuffers[kiLogCategoryCount];
extern LogGlobalBuffer gLogGlobalBuffer;

inline bool ShouldLog(LogLevel eLevel, LogCategory eCategory)
{
	if (eLevel == LogLevel::kError) [[unlikely]]
		return true;
	if (eCategory == geFocusedLogCategory)
		return true;
	return eLevel >= geLogLevel;
}

void LogIndent(int64_t iIndent);
char* LogPrefix(char* pLogBuffer);
void LogWrite(char* pLogBuffer);
void LogWriteRingBuffers(const char* pLogBuffer, LogCategory eCategory);
void LogDumpBuffers(std::ofstream& rOfstream);

// (1) Full form: category + level + format
template <typename... TUV>
void Log(LogCategory eCategory, LogLevel eLevel, std::format_string<const TUV&...> format, const TUV&... parameters)
{
	if constexpr (kbLogging)
	{
		if (!ShouldLog(eLevel, eCategory)) [[likely]]
		{
			return;
		}

		static char spLogBuffer[kiLogBufferSize] {};
		char* pLogBuffer = gpThreadLocal != nullptr ? gpThreadLocal->mpLogBuffer : spLogBuffer;
		char* pWrite = LogPrefix(pLogBuffer);

		pWrite = std::format_to(pWrite, format, parameters...);

		*(pWrite++) = '\n';
		*(pWrite++) = 0;

		LogWriteRingBuffers(pLogBuffer, eCategory);
		LogWrite(pLogBuffer);
	}
}

// (2) Category + format (default level = kDebug)
template <typename... TUV>
void Log(LogCategory eCategory, std::format_string<const TUV&...> format, const TUV&... parameters)
{
	Log(eCategory, LogLevel::kDebug, format, parameters...);
}

// (3) Level + format (default category = kDefault)
template <typename... TUV>
void Log(LogLevel eLevel, std::format_string<const TUV&...> format, const TUV&... parameters)
{
	Log(LogCategory::kDefault, eLevel, format, parameters...);
}

// (4) Format only (kDefault + kDebug)
template <typename... TUV>
void Log(std::format_string<const TUV&...> format, const TUV&... parameters)
{
	Log(LogCategory::kDefault, LogLevel::kDebug, format, parameters...);
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

// Level aliases
inline constexpr auto kVerbose = common::LogLevel::kVerbose;
inline constexpr auto kDebug   = common::LogLevel::kDebug;
inline constexpr auto kWarning = common::LogLevel::kWarning;
inline constexpr auto kError   = common::LogLevel::kError;

// Category aliases (kLog* prefix avoids collisions with ChunkFlags::kAudio etc.)
inline constexpr auto kLogAudio    = common::LogCategory::kAudio;
inline constexpr auto kLogDefault  = common::LogCategory::kDefault;
inline constexpr auto kLogGraphics = common::LogCategory::kGraphics;
inline constexpr auto kLogLoading  = common::LogCategory::kLoading;
inline constexpr auto kLogNetwork  = common::LogCategory::kNetwork;

using common::LogLevel;
using common::LogCategory;
using common::Log;
using common::LogIndent;
using common::ScopedLogIndent;
