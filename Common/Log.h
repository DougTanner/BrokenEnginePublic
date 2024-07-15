#pragma once

#include "LogFormatters.h"
#include "ThreadLocal.h"

namespace common
{

inline std::atomic<int64_t> giMyOutputDebugString = 0;
inline std::ofstream* gpLogFileStream = nullptr;

inline void LogIndent(int64_t iIndent)
{
	gpThreadLocal->miLogIndent += iIndent;
}

inline std::mutex gLogMutex;

template<typename... TUV>
void Log(std::string_view pcFormat, const TUV&... parameters)
{
	static std::array<char, kiLogBufferSize> sLogBuffer {};
	std::array<char, kiLogBufferSize>& rLogBuffer = gpThreadLocal != nullptr ? *gpThreadLocal->mpLogBuffer : sLogBuffer;
	auto it = rLogBuffer.begin();

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

	it = std::vformat_to(it, pcFormat.data(), std::make_format_args(parameters...));

	*(it++) = '\n';
	*(it++) = 0;

#if defined(BT_DATA_PACKER)
	std::lock_guard lockGuard(gLogMutex);
#endif
	++giMyOutputDebugString;
	OutputDebugString(rLogBuffer.data());
	--giMyOutputDebugString;
#if defined(BT_DATA_PACKER)
	printf(rLogBuffer.data());
#endif
	if (gpLogFileStream)
	{
	#if !defined(BT_DATA_PACKER)
		std::lock_guard lockGuard(gLogMutex);
	#endif
		*gpLogFileStream << rLogBuffer.data() << std::flush;
	}
}

}
