#include "WindowsUtils.h"

namespace common
{

std::string_view LastErrorString()
{
	static char spcReturn[MAX_PATH] {};
	spcReturn[0] = 0;
	return std::string_view(spcReturn, FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), spcReturn, static_cast<DWORD>(std::size(spcReturn)) - 1, nullptr));
}

std::string_view HresultToString(HRESULT hresult)
{
	static char spcReturn[MAX_PATH] {};
	spcReturn[0] = 0;
	return std::string_view(spcReturn, FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(hresult), 0, spcReturn, static_cast<DWORD>(std::size(spcReturn) - 1), nullptr));
}

int64_t LogicalCoreCount()
{
	int64_t iLogicalCoreCount = std::thread::hardware_concurrency();
	if (iLogicalCoreCount == 0)
	{
		Log("std::thread::hardware_concurrency() returned 0");
		iLogicalCoreCount = 1;
	}

	return iLogicalCoreCount;
}

int64_t HardwareCoreCount()
{
	using LPFN_GLPI = BOOL(WINAPI*)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);
	HMODULE hmodule = GetModuleHandle(TEXT("kernel32"));
	if (hmodule == nullptr)
	{
		Log("GetModuleHandle(TEXT(\"kernel32\")) returned nullptr");
		return LogicalCoreCount();
	}

	LPFN_GLPI glpi = reinterpret_cast<LPFN_GLPI>(GetProcAddress(hmodule, "GetLogicalProcessorInformation"));
	if (glpi == nullptr)
	{
		Log("GetProcAddress(hmodule, \"GetLogicalProcessorInformation\") returned nullptr");
		return LogicalCoreCount();
	}

	std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(1);
	DWORD uiReturnLength = static_cast<DWORD>(buffer.size() * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));

	BOOL bDone = FALSE;
	while (!bDone)
	{
		DWORD rc = glpi(buffer.data(), &uiReturnLength);

		if (rc == FALSE)
		{
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				buffer.resize(uiReturnLength / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
			}
			else
			{
				Log("glpi GetLastError: {}", GetLastError());
				return LogicalCoreCount();
			}
		}
		else
		{
			bDone = TRUE;
		}
	}

	DWORD uiProcessorCoreCount = 0;
	for (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION& rSystemLogicalProcessorInformation : buffer)
	{
		if (rSystemLogicalProcessorInformation.Relationship == RelationProcessorCore)
		{
			++uiProcessorCoreCount;
		}
	}

	if (uiProcessorCoreCount >= 1)
	{
		return uiProcessorCoreCount;
	}
	else
	{
		return LogicalCoreCount();
	}
}

std::tuple<std::string, std::string> FileTimeString(const std::filesystem::file_time_type& rFileTime)
{
	SYSTEMTIME systemtime {};
	VERIFY_SUCCESS(FileTimeToSystemTime(reinterpret_cast<const FILETIME*>(&rFileTime), &systemtime));
	SYSTEMTIME localSystemtime {};
	VERIFY_SUCCESS(SystemTimeToTzSpecificLocalTime(nullptr, &systemtime, &localSystemtime));

	char pcDate[MAX_PATH] {};
	int iWritten = GetDateFormat(LOCALE_USER_DEFAULT, 0, &localSystemtime, "yyyy-MM-dd", pcDate, static_cast<DWORD>(std::size(pcDate) - 1));
	ASSERT(iWritten != 0);

	char pcTime[MAX_PATH] {};
	iWritten = GetTimeFormat(LOCALE_USER_DEFAULT, 0, &localSystemtime, "h:mm tt", pcTime, static_cast<DWORD>(std::size(pcTime) - 1));
	ASSERT(iWritten != 0);

	return std::make_tuple(std::string(pcDate), std::string(pcTime));
}

ExecutableResult RunExecutable(const std::filesystem::path& rExecutableFile, std::wstring& rCommandLine)
{
	SECURITY_ATTRIBUTES securityAttributes
	{
		.nLength = sizeof(SECURITY_ATTRIBUTES),
		.lpSecurityDescriptor = nullptr,
		.bInheritHandle = TRUE,
	};

	HANDLE hStdInPipeRead = nullptr;
	HANDLE hStdInPipeWrite = nullptr;
	HANDLE hStdOutPipeRead = nullptr;
	HANDLE hStdOutPipeWrite = nullptr;
	VERIFY_SUCCESS(CreatePipe(&hStdInPipeRead, &hStdInPipeWrite, &securityAttributes, 0));
	VERIFY_SUCCESS(CreatePipe(&hStdOutPipeRead, &hStdOutPipeWrite, &securityAttributes, 0));

	STARTUPINFOW startupinfow
	{
		.cb = sizeof(STARTUPINFOW),
		.dwFlags = STARTF_USESTDHANDLES,
		.hStdInput = hStdInPipeRead,
		.hStdOutput = hStdOutPipeWrite,
		.hStdError = hStdOutPipeWrite,
	};
	PROCESS_INFORMATION processInformation {};
	VERIFY_SUCCESS(CreateProcessW(rExecutableFile.native().c_str(), rCommandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startupinfow, &processInformation));

	CloseHandle(hStdOutPipeWrite);
	CloseHandle(hStdInPipeRead);

	std::string output;
	char pcPipeOutput[1024] {};
	DWORD uiBytesRead = 0;
	while (ReadFile(hStdOutPipeRead, pcPipeOutput, static_cast<DWORD>(sizeof(pcPipeOutput) - 1), &uiBytesRead, nullptr) == TRUE)
	{
		pcPipeOutput[uiBytesRead] = 0;
		output.append(pcPipeOutput, &pcPipeOutput[uiBytesRead]);
	}

	WaitForSingleObject(processInformation.hProcess, INFINITE);
	DWORD uiExitCode = 0;
	GetExitCodeProcess(processInformation.hProcess, &uiExitCode);

	CloseHandle(hStdOutPipeRead);
	CloseHandle(hStdInPipeWrite);
	CloseHandle(processInformation.hThread);
	CloseHandle(processInformation.hProcess);

	return {.mOutput = std::move(output), .miExitCode = static_cast<int64_t>(uiExitCode)};
}

} // namespace common
