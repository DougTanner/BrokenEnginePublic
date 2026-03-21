#include "Log.h"

namespace common
{

uint64_t guiLogEnabledCategories = kLogEnabledCategoriesDefault;
std::atomic<int64_t> giMyOutputDebugString = 0;
std::ofstream* gpLogFileStream = nullptr;
std::mutex gLogMutex;

void LogIndent(int64_t iIndent)
{
	if constexpr (kbEnableLogging)
	{
		gpThreadLocal->miLogIndent += iIndent;
	}
}

char* LogPrefix(char* pLogBuffer)
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

void LogWrite(char* pLogBuffer)
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

} // namespace common
