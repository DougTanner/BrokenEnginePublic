#include "Log.h"

namespace common
{

std::atomic<int64_t> giMyOutputDebugString = 0;
std::mutex gLogMutex;

LogRingBuffer gLogRingBuffers[kiLogCategoryCount];
LogGlobalBuffer gLogGlobalBuffer;

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
			*(pWrite++) = c;
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
		printf("%s", pLogBuffer);
	}
}

void LogWriteRingBuffers(const char* pLogBuffer, int64_t iLength, LogCategory eCategory)
{
	auto copyToLine = [pLogBuffer, iLength](char* pLine)
	{
		if (pLine == nullptr)
			return;
		std::memcpy(pLine, pLogBuffer, iLength);
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
