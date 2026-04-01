#include "Log.h"

namespace common
{

LogLevel geLogLevel = keLogLevelDefault;
LogCategory geFocusedLogCategory = keFocusedLogCategoryDefault;
std::atomic<int64_t> giMyOutputDebugString = 0;
std::mutex gLogMutex;

LogRingBuffer gLogRingBuffers[kiLogCategoryCount];
LogGlobalBuffer gLogGlobalBuffer;

void LogIndent(int64_t iIndent)
{
	if constexpr (kbLogging)
	{
		gpThreadLocal->miLogIndent += iIndent;
	}
}

char* LogPrefix(char* pLogBuffer)
{
	char* pWrite = pLogBuffer;

	if (gpThreadLocal != nullptr) [[likely]]
	{
		int64_t iLogIndent = gpThreadLocal->miLogIndent;
		for (int64_t i = 0; i < iLogIndent; ++i)
		{
			*(pWrite++) = ' ';
			*(pWrite++) = ' ';
		}

		if (gpThreadLocal->miThreadId.has_value())
		{
			if (gpThreadLocal->miThreadId.value() >= 100)
			{
				std::to_chars(&*pWrite, &*pWrite + 3, gpThreadLocal->miThreadId.value());
				++pWrite; ++pWrite; ++pWrite;
			}
			else if (gpThreadLocal->miThreadId.value() >= 10)
			{
				std::to_chars(&*pWrite, &*pWrite + 2, gpThreadLocal->miThreadId.value());
				++pWrite; ++pWrite;
			}
			else
			{
				std::to_chars(&*pWrite, &*pWrite + 1, gpThreadLocal->miThreadId.value());
				++pWrite;
			}

			*(pWrite++) = ':';
			*(pWrite++) = ' ';
		}
	}
	else
	{
		*(pWrite++) = '#';
		*(pWrite++) = ':';
		*(pWrite++) = ' ';
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
		printf("%s", pLogBuffer);
	}
}

void LogWriteRingBuffers(const char* pLogBuffer, LogCategory eCategory)
{
	auto copyToLine = [pLogBuffer](char* pLine)
	{
		if (pLine == nullptr)
			return;
		int64_t iLen = std::min(static_cast<int64_t>(strlen(pLogBuffer)), kiLogBufferSize - 2);
		memcpy(pLine, pLogBuffer, iLen + 1);
	};

	copyToLine(gLogRingBuffers[static_cast<int64_t>(eCategory)].AcquireLine());
	copyToLine(gLogGlobalBuffer.AcquireLine());
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
