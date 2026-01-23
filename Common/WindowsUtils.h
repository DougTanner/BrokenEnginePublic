#pragma once

namespace common
{

// Converts the last Windows API error code to a human-readable string
// Uses GetLastError() to retrieve the error code and FormatMessage() to convert it
// Returns: std::string_view containing the formatted error message
// Thread-safety: NOT THREAD-SAFE - uses static buffer that is shared across all calls
// Subsequent calls will overwrite the buffer, so copy the string if needed
inline std::string_view LastErrorString()
{
	static char spcReturn[MAX_PATH] {};
	spcReturn[0] = 0;
	return std::string_view(spcReturn, FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), spcReturn, static_cast<DWORD>(std::size(spcReturn)) - 1, nullptr));
}

// Converts an HRESULT error code to a human-readable string
// Parameters:
//   hresult - The HRESULT error code to convert (e.g., from DirectX, COM, or Windows APIs)
// Returns: std::string_view containing the formatted error message
// Thread-safety: NOT THREAD-SAFE - uses static buffer that is shared across all calls
// Subsequent calls will overwrite the buffer, so copy the string if needed
inline std::string_view HresultToString(HRESULT hresult)
{
	static char spcReturn[MAX_PATH] {};
	spcReturn[0] = 0;
	return std::string_view(spcReturn, FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(hresult), 0, spcReturn, static_cast<DWORD>(std::size(spcReturn) - 1), nullptr));
}

// Converts a filesystem file time to formatted date and time strings in user's locale
// Parameters:
//   rFileTime - The std::filesystem::file_time_type to convert (typically from std::filesystem::last_write_time)
// Returns: std::tuple<std::string, std::string> containing (date, time)
//   - Date format: "yyyy-MM-dd" (e.g., "2024-01-15")
//   - Time format: "h:mm tt" (e.g., "3:45 PM")
// Converts to user's local timezone using SystemTimeToTzSpecificLocalTime()
// Thread-safety: Thread-safe - uses local stack buffers
inline std::tuple<std::string, std::string> FileTimeString(const std::filesystem::file_time_type& rFileTime)
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

// Executes an external process with captured stdin/stdout/stderr and returns the output
// Parameters:
//   rExecutableFile - Full path to the executable to run
//   rCommandLine - Command-line arguments (wide string). Modified by CreateProcessW so cannot be const
// Returns: std::string containing all output from stdout and stderr combined
// Creates pipes for process communication, runs the process with CREATE_NO_WINDOW flag,
// captures all output until the process terminates or closes its output handles
// Used by DataPacker to run build tools like glslangValidator and ffmpeg
// Thread-safety: Thread-safe - uses local resources and process isolation
inline std::string RunExecutable(const std::filesystem::path& rExecutableFile, std::wstring& rCommandLine)
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
	// WaitForSingleObject(processInformation.hProcess, INFINITE);

	std::string output;
	char pcPipeOutput[1024] {};
	DWORD uiBytesRead = 0;
	while (ReadFile(hStdOutPipeRead, pcPipeOutput, static_cast<DWORD>(sizeof(pcPipeOutput) - 1), &uiBytesRead, nullptr) == TRUE)
	{
		pcPipeOutput[uiBytesRead] = 0;
		output.append(pcPipeOutput, &pcPipeOutput[uiBytesRead]);
	}

	CloseHandle(hStdOutPipeRead);
	CloseHandle(hStdInPipeWrite);
	CloseHandle(processInformation.hThread);
	CloseHandle(processInformation.hProcess);

	return output;
}

// Returns the number of logical CPU cores including hyperthreading
// Uses std::thread::hardware_concurrency() to query the system
// Returns: int64_t number of logical cores (minimum 1)
//   - On hyperthreaded systems, returns physical cores × 2
//   - If hardware_concurrency() fails (returns 0), defaults to 1 and logs a warning
// Thread-safety: Thread-safe - standard library call
inline int64_t LogicalCoreCount()
{
	int64_t iLogicalCoreCount = std::thread::hardware_concurrency();
	if (iLogicalCoreCount == 0)
	{
		Log("std::thread::hardware_concurrency() returned 0");
		iLogicalCoreCount = 1;
	}

	return iLogicalCoreCount;
}

// Returns the number of physical CPU cores excluding hyperthreading
// Uses Windows GetLogicalProcessorInformation() API to query actual hardware cores
// Returns: int64_t number of physical cores (minimum 1)
//   - Counts only cores with RelationProcessorCore relationship
//   - Falls back to LogicalCoreCount() if the API is unavailable or fails
//   - Dynamically resizes buffer if ERROR_INSUFFICIENT_BUFFER is returned
// Thread-safety: Thread-safe - uses local resources and Windows API
// Use this for determining optimal worker thread counts to avoid hyperthreading overhead
inline int64_t HardwareCoreCount()
{
	using LPFN_GLPI = BOOL(WINAPI*)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);
	HMODULE hmodule = GetModuleHandle(TEXT("kernel32"));
	if (hmodule == nullptr)
	{
		Log("GetModuleHandle(TEXT(\"kernel32\")) returned nullptr");
		return LogicalCoreCount();
	}

	LPFN_GLPI glpi = (LPFN_GLPI)GetProcAddress(hmodule, "GetLogicalProcessorInformation");
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

} // namespace common
