#pragma once

#include "LogTypes.h"
#include "LogFormatters.h"
#include "Threading/ThreadLocal.h"

namespace common
{

// Category names for crash dump labels
inline constexpr const char* kpcLogCategoryNames[] = {"Default", "Temp", "Audio", "Graphics", "Loading", "NavData", "Network", "Input"};

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

void LogIndent(int64_t iIndent);
char* LogPrefix(char* pLogBuffer, char* pEnd);
void LogWrite(char* pLogBuffer);
void LogWriteRingBuffers(const char* pLogBuffer, int64_t iLength, LogCategory eCategory);
void LogDumpBuffers(std::ofstream& rOfstream);

// Unfiltered log writer — called by LOG() macro after compile-time filtering
template <typename... TUV>
void Log(LogCategory eCategory, std::format_string<const TUV&...> format, const TUV&... parameters)
{
	static char spcLogBuffer[kiLogBufferSize] {};
	char* pLogBuffer = gpThreadLocal != nullptr ? gpThreadLocal->mpLogBuffer : spcLogBuffer;
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

// Compile-time log filtering: if constexpr eliminates filtered-out calls entirely
#define LOG(category, level, format, ...) \
do { \
	if constexpr (kbLogging && (level) >= keLogLevels[static_cast<int64_t>(category)]) \
	{ \
		common::Log((category), format __VA_OPT__(,) __VA_ARGS__); \
	} \
} while (false)
