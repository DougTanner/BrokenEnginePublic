#include "WindowsUtils.h"

namespace common
{

// FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, ...) appends a trailing "\r\n" (often with a '.') and reports a
// length that includes it; trimming keeps single-line log output clean. Also NUL-terminates at the trim point.
static std::string_view TrimSystemMessage(char* pcBuffer, DWORD uiLength)
{
	while (uiLength > 0 && (pcBuffer[uiLength - 1] == '\r' || pcBuffer[uiLength - 1] == '\n' || pcBuffer[uiLength - 1] == '.' || pcBuffer[uiLength - 1] == ' '))
	{
		--uiLength;
	}

	pcBuffer[uiLength] = 0;
	return std::string_view(pcBuffer, uiLength);
}

std::string LastErrorString()
{
	char acReturn[MAX_PATH] {};
	DWORD uiLength = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), acReturn, static_cast<DWORD>(std::size(acReturn)) - 1, nullptr);
	return std::string(TrimSystemMessage(acReturn, uiLength));
}

std::string HresultToString(HRESULT hresult)
{
	char acReturn[MAX_PATH] {};
	DWORD uiLength = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(hresult), 0, acReturn, static_cast<DWORD>(std::size(acReturn) - 1), nullptr);
	return std::string(TrimSystemMessage(acReturn, uiLength));
}

int64_t LogicalCoreCount()
{
	int64_t iLogicalCoreCount = std::thread::hardware_concurrency();
	if (iLogicalCoreCount == 0)
	{
		LOG(kDefault, kDebug, "std::thread::hardware_concurrency() returned 0");
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
		LOG(kDefault, kWarning, "GetModuleHandle(TEXT(\"kernel32\")) returned nullptr");
		return LogicalCoreCount();
	}

	LPFN_GLPI glpi = reinterpret_cast<LPFN_GLPI>(GetProcAddress(hmodule, "GetLogicalProcessorInformation"));
	if (glpi == nullptr)
	{
		LOG(kDefault, kWarning, "GetProcAddress(hmodule, \"GetLogicalProcessorInformation\") returned nullptr");
		return LogicalCoreCount();
	}

	std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(1);
	DWORD uiReturnLength = static_cast<DWORD>(buffer.size() * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));

	BOOL bDone = FALSE;
	while (bDone == FALSE)
	{
		BOOL bSuccess = glpi(buffer.data(), &uiReturnLength);

		if (bSuccess == FALSE)
		{
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				buffer.resize(uiReturnLength / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
			}
			else
			{
				LOG(kDefault, kWarning, "glpi GetLastError: {}", GetLastError());
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
	// file_time_type's clock has no standard layout/epoch relationship to FILETIME — aliasing one through the
	// other is UB that only works by MSVC-STL coincidence. Convert through system_clock (leap-second-naive, like
	// FileTimeToSystemTime) and rebuild the FILETIME from its 100ns tick count since the 1601 epoch.
	const std::chrono::system_clock::time_point systemClockTime = std::chrono::clock_cast<std::chrono::system_clock>(rFileTime);
	const uint64_t uiHundredNsSince1601 = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10'000'000>>>(systemClockTime.time_since_epoch()).count() + 116'444'736'000'000'000);
	const FILETIME filetime
	{
		.dwLowDateTime = static_cast<DWORD>(uiHundredNsSince1601 & 0xFFFFFFFF),
		.dwHighDateTime = static_cast<DWORD>(uiHundredNsSince1601 >> 32),
	};
	SYSTEMTIME systemtime {};
	VERIFY_SUCCESS(FileTimeToSystemTime(&filetime, &systemtime));
	SYSTEMTIME localSystemtime {};
	VERIFY_SUCCESS(SystemTimeToTzSpecificLocalTime(nullptr, &systemtime, &localSystemtime));

	// OS trust boundary: format failures degrade to empty display strings instead of crashing
	char pcDate[MAX_PATH] {};
	int iWritten = GetDateFormat(LOCALE_USER_DEFAULT, 0, &localSystemtime, "yyyy-MM-dd", pcDate, static_cast<DWORD>(std::size(pcDate) - 1));
	if (iWritten == 0)
	{
		LOG(kDefault, kWarning, "GetDateFormat failed: {}", GetLastError());
	}

	char pcTime[MAX_PATH] {};
	iWritten = GetTimeFormat(LOCALE_USER_DEFAULT, 0, &localSystemtime, "h:mm tt", pcTime, static_cast<DWORD>(std::size(pcTime) - 1));
	if (iWritten == 0)
	{
		LOG(kDefault, kWarning, "GetTimeFormat failed: {}", GetLastError());
	}

	return std::make_tuple(std::string(pcDate), std::string(pcTime));
}

ExecutableResult RunExecutable(const std::filesystem::path& rExecutableFile, std::wstring& rCommandLine)
{
	// RAII so any throwing VERIFY_SUCCESS below unwinds without leaking the handle / attribute list. Pipe and
	// process handles use a nullptr sentinel, so unique_ptr<void> (which skips the deleter on nullptr) fits.
	using ScopedHandle = std::unique_ptr<void, decltype(&CloseHandle)>;

	SECURITY_ATTRIBUTES securityAttributes
	{
		.nLength = sizeof(SECURITY_ATTRIBUTES),
		.lpSecurityDescriptor = nullptr,
		.bInheritHandle = TRUE,
	};

	HANDLE hStdInPipeRead = nullptr;
	HANDLE hStdInPipeWrite = nullptr;
	VERIFY_SUCCESS(CreatePipe(&hStdInPipeRead, &hStdInPipeWrite, &securityAttributes, 0));
	ScopedHandle pStdInPipeRead(hStdInPipeRead, &CloseHandle);
	ScopedHandle pStdInPipeWrite(hStdInPipeWrite, &CloseHandle);

	HANDLE hStdOutPipeRead = nullptr;
	HANDLE hStdOutPipeWrite = nullptr;
	VERIFY_SUCCESS(CreatePipe(&hStdOutPipeRead, &hStdOutPipeWrite, &securityAttributes, 0));
	ScopedHandle pStdOutPipeRead(hStdOutPipeRead, &CloseHandle);
	ScopedHandle pStdOutPipeWrite(hStdOutPipeWrite, &CloseHandle);

	// Strip inheritance from parent-side pipe ends; the attribute list below only applies to the child-side two.
	VERIFY_SUCCESS(SetHandleInformation(hStdInPipeWrite, HANDLE_FLAG_INHERIT, 0));
	VERIFY_SUCCESS(SetHandleInformation(hStdOutPipeRead, HANDLE_FLAG_INHERIT, 0));

	// STARTUPINFOEX + PROC_THREAD_ATTRIBUTE_HANDLE_LIST whitelists exactly the two pipe ends the child needs.
	// Without this, the child inherits every HANDLE_FLAG_INHERIT=1 handle in the parent (log files, random
	// framework handles, etc.) — harmless for native C children like glslc, but .NET children like Gaea.Swarm
	// inspect inherited stdio handles during runtime startup and FailFast when they find unexpected extras.
	SIZE_T uiAttributeListSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &uiAttributeListSize);
	auto attributeListBuffer = std::make_unique<uint8_t[]>(uiAttributeListSize);
	LPPROC_THREAD_ATTRIBUTE_LIST pAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeListBuffer.get());
	_Analysis_assume_(pAttributeList != nullptr);
	VERIFY_SUCCESS(InitializeProcThreadAttributeList(pAttributeList, 1, 0, &uiAttributeListSize));
	// Tears down before attributeListBuffer frees (reverse declaration order) so the list outlives its backing.
	ScopedLambda attributeListGuard([pAttributeList]()
	{
		DeleteProcThreadAttributeList(pAttributeList);
	});
	HANDLE inheritHandles[] = { hStdInPipeRead, hStdOutPipeWrite };
	VERIFY_SUCCESS(UpdateProcThreadAttribute(pAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritHandles, sizeof(inheritHandles), nullptr, nullptr));

	STARTUPINFOEXW startupinfoex {};
	startupinfoex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
	startupinfoex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startupinfoex.StartupInfo.hStdInput = hStdInPipeRead;
	startupinfoex.StartupInfo.hStdOutput = hStdOutPipeWrite;
	startupinfoex.StartupInfo.hStdError = hStdOutPipeWrite;
	startupinfoex.lpAttributeList = pAttributeList;

	PROCESS_INFORMATION processInformation {};
	VERIFY_SUCCESS(CreateProcessW(rExecutableFile.native().c_str(), rCommandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startupinfoex.StartupInfo, &processInformation));
	ScopedHandle pProcess(processInformation.hProcess, &CloseHandle);
	ScopedHandle pThread(processInformation.hThread, &CloseHandle);

	// Close the parent's child-side pipe ends now (before the read loop, not at scope exit) so ReadFile sees
	// EOF once the child exits; the child holds its own inherited duplicates.
	pStdOutPipeWrite.reset();
	pStdInPipeRead.reset();

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

	return {.mOutput = std::move(output), .miExitCode = static_cast<int64_t>(uiExitCode)};
}

void LaunchExecutable(const std::filesystem::path& rExecutableFile)
{
	STARTUPINFOW startupinfow {};
	startupinfow.cb = sizeof(STARTUPINFOW);
	PROCESS_INFORMATION processInformation {};
	std::wstring commandLine = rExecutableFile.native();
	CreateProcessW(rExecutableFile.native().c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startupinfow, &processInformation);

	CloseHandle(processInformation.hThread);
	CloseHandle(processInformation.hProcess);
}

ExecutableResult RunExecutableInNewConsole(const std::filesystem::path& rExecutableFile, std::wstring& rCommandLine)
{
	// CREATE_NEW_CONSOLE gives the child real console handles for stdin/stdout/stderr — required
	// for tools like Gaea.Swarm.exe that throw IOException("The handle is invalid") when their
	// console is a pipe or file. SW_HIDE keeps the new window off-screen so the bake doesn't
	// flash UI during a build.
	STARTUPINFOW startupinfow {};
	startupinfow.cb = sizeof(STARTUPINFOW);
	startupinfow.dwFlags = STARTF_USESHOWWINDOW;
	startupinfow.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION processInformation {};
	VERIFY_SUCCESS(CreateProcessW(rExecutableFile.native().c_str(), rCommandLine.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &startupinfow, &processInformation));

	WaitForSingleObject(processInformation.hProcess, INFINITE);

	DWORD uiExitCode = 0;
	GetExitCodeProcess(processInformation.hProcess, &uiExitCode);

	CloseHandle(processInformation.hThread);
	CloseHandle(processInformation.hProcess);

	return {.mOutput = std::string {}, .miExitCode = static_cast<int64_t>(uiExitCode)};
}

} // namespace common
