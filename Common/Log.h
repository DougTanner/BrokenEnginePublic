#pragma once

#include "LogFormatters.h"
#include "ThreadLocal.h"

namespace common
{

// Log categories (bitmask)
inline constexpr uint64_t kLogDefault = 1ULL << 0;
inline constexpr uint64_t kLogLoading = 1ULL << 1;
inline constexpr uint64_t kLogNetwork = 1ULL << 2;

// Enabled categories mask (set directly to change filtering)
inline uint64_t gLogEnabledCategories = kLogNetwork; // DT: TEMP kLogDefault;

inline std::atomic<int64_t> giMyOutputDebugString = 0;
inline std::ofstream* gpLogFileStream = nullptr;

inline void LogIndent(int64_t iIndent)
{
	if constexpr (kbEnableLogging)
	{
		gpThreadLocal->miLogIndent += iIndent;
	}
}

inline std::mutex gLogMutex;

// Write indent and thread ID prefix, return pointer past prefix
inline char* LogPrefix(char* pLogBuffer)
{
	char* it = pLogBuffer;

	if (gpThreadLocal != nullptr) [[likely]]
	{
		int64_t iLogIndent = gpThreadLocal->miLogIndent;
		for (int64_t i = 0; i < iLogIndent; ++i)
		{
			*(it++) = ' ';
			*(it++) = ' ';
		}

		if (gpThreadLocal->miThreadId.has_value())
		{
			if (gpThreadLocal->miThreadId.value() >= 100)
			{
				std::to_chars(&*it, &*it + 3, gpThreadLocal->miThreadId.value());
				++it; ++it; ++it;
			}
			else if (gpThreadLocal->miThreadId.value() >= 10)
			{
				std::to_chars(&*it, &*it + 2, gpThreadLocal->miThreadId.value());
				++it; ++it;
			}
			else
			{
				std::to_chars(&*it, &*it + 1, gpThreadLocal->miThreadId.value());
				++it;
			}

			*(it++) = ':';
			*(it++) = ' ';
		}
	}
	else
	{
		*(it++) = '#';
		*(it++) = ':';
		*(it++) = ' ';
	}

	return it;
}

// Write formatted buffer to debug output, printf, and log file
inline void LogWrite(char* pLogBuffer)
{
	std::unique_lock lockGuard(gLogMutex);
	++giMyOutputDebugString;
	OutputDebugString(pLogBuffer);
	--giMyOutputDebugString;
	if constexpr (kbAlsoLogToPrintf)
	{
		printf("%s", pLogBuffer);
	}
	if (gpLogFileStream != nullptr)
	{
		*gpLogFileStream << pLogBuffer << std::flush;
	}
}

template<typename... TUV>
void Log(uint64_t uiCategory, std::format_string<const TUV&...> format, const TUV&... parameters)
{
	if constexpr (kbEnableLogging)
	{
		if (!(gLogEnabledCategories & uiCategory)) [[unlikely]]
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

template<typename... TUV>
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
using common::Log;
using common::LogIndent;
using common::ScopedLogIndent;
