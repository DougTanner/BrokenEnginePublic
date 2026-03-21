#pragma once

#include "LogFormatters.h"
#include "ThreadLocal.h"

namespace common
{

// Log categories (bitmask)
inline constexpr uint64_t kLogDefault = 1ULL << 0;
inline constexpr uint64_t kLogLoading = 1ULL << 1;
inline constexpr uint64_t kLogNetwork = 1ULL << 2;
inline constexpr uint64_t kLogError = 1ULL << 3;
inline constexpr uint64_t kLogAudio = 1ULL << 4;

// Mutable globals (defined in Log.cpp)
extern uint64_t guiLogEnabledCategories;
extern std::atomic<int64_t> giMyOutputDebugString;
extern std::ofstream* gpLogFileStream;
extern std::mutex gLogMutex;

void LogIndent(int64_t iIndent);
char* LogPrefix(char* pLogBuffer);
void LogWrite(char* pLogBuffer);

template <typename... TUV>
void Log(uint64_t uiCategory, std::format_string<const TUV&...> format, const TUV&... parameters)
{
	if constexpr (kbEnableLogging)
	{
		if (!(uiCategory & kLogError) && !(guiLogEnabledCategories & uiCategory)) [[unlikely]]
		{
			return;
		}

		static char spLogBuffer[kiLogBufferSize] {};
		char* pLogBuffer = gpThreadLocal != nullptr ? gpThreadLocal->mpLogBuffer : spLogBuffer;
		char* it = LogPrefix(pLogBuffer);

		it = std::format_to(it, format, parameters...);

		*(it++) = '\n';
		*(it++) = 0;

		LogWrite(pLogBuffer);
	}
}

template <typename... TUV>
void Log(std::format_string<const TUV&...> format, const TUV&... parameters)
{
	Log(kLogDefault, format, parameters...);
}

class ScopedLogIndent
{
public:

	ScopedLogIndent()
	{
		if constexpr (kbEnableLogging)
		{
			LogIndent(1);
		}
	}

	~ScopedLogIndent()
	{
		if constexpr (kbEnableLogging)
		{
			LogIndent(-1);
		}
	}

	ScopedLogIndent(const ScopedLogIndent&) = delete;
	ScopedLogIndent& operator=(const ScopedLogIndent&) = delete;
};

} // namespace common

using common::kLogDefault;
using common::kLogLoading;
using common::kLogNetwork;
using common::kLogError;
using common::kLogAudio;
using common::Log;
using common::LogIndent;
using common::ScopedLogIndent;
