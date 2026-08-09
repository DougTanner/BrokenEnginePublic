#include "ToolCliCommon.h"

#include <bcrypt.h>

#include <cstdio>
#include <iostream>
#include <utility>

namespace toolcli
{
	namespace
	{
		std::string sToolName = "ToolCli";

		void ReportProcessFailure(const RunProcessOptions& rOptions, std::string_view operation)
		{
			const DWORD uiError = ::GetLastError();
			if (rOptions.failureSink)
			{
				rOptions.failureSink(std::string(operation) + " failed (Windows error " + std::to_string(uiError) + ")");
			}
		}
	}

	Handle::Handle(HANDLE hHandle) :
		mhHandle(hHandle)
	{
	}

	Handle::~Handle()
	{
		Reset();
	}

	Handle::Handle(Handle&& rOther) noexcept :
		mhHandle(rOther.Release())
	{
	}

	Handle& Handle::operator=(Handle&& rOther) noexcept
	{
		if (this != &rOther)
		{
			Reset(rOther.Release());
		}
		return *this;
	}

	HANDLE Handle::Get() const
	{
		return mhHandle;
	}

	bool Handle::IsValid() const
	{
		return mhHandle != nullptr && mhHandle != INVALID_HANDLE_VALUE;
	}

	HANDLE Handle::Release()
	{
		HANDLE hHandle = mhHandle;
		mhHandle = INVALID_HANDLE_VALUE;
		return hHandle;
	}

	void Handle::Reset(HANDLE hHandle)
	{
		if (IsValid())
		{
			::CloseHandle(mhHandle);
		}
		mhHandle = hHandle;
	}

	std::optional<ProcessResult> RunProcess(const std::filesystem::path* pExecutable, const std::vector<std::wstring>& rArguments, const RunProcessOptions& rOptions)
	{
		Handle hJob;
		if (rOptions.bKillOnJobClose)
		{
			hJob.Reset(::CreateJobObjectW(nullptr, nullptr));
			if (!hJob.IsValid())
			{
				ReportProcessFailure(rOptions, "create process job");
				return std::nullopt;
			}
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInformation {};
			jobInformation.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			if (::SetInformationJobObject(hJob.Get(), JobObjectExtendedLimitInformation, &jobInformation, sizeof(jobInformation)) == FALSE)
			{
				ReportProcessFailure(rOptions, "configure process job");
				return std::nullopt;
			}
		}

		SECURITY_ATTRIBUTES pipeAttributes { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		Handle hPipeRead;
		Handle hPipeWrite;
		if (rOptions.bCaptureOutput)
		{
			HANDLE hRawRead = INVALID_HANDLE_VALUE;
			HANDLE hRawWrite = INVALID_HANDLE_VALUE;
			if (::CreatePipe(&hRawRead, &hRawWrite, &pipeAttributes, 0) == FALSE)
			{
				ReportProcessFailure(rOptions, "create output pipe");
				return std::nullopt;
			}
			hPipeRead.Reset(hRawRead);
			hPipeWrite.Reset(hRawWrite);
			if (::SetHandleInformation(hPipeRead.Get(), HANDLE_FLAG_INHERIT, 0) == FALSE)
			{
				ReportProcessFailure(rOptions, "configure output pipe");
				return std::nullopt;
			}
		}

		STARTUPINFOW startupInfo {};
		startupInfo.cb = sizeof(startupInfo);
		startupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
		startupInfo.hStdOutput = rOptions.bCaptureOutput ? hPipeWrite.Get() : ::GetStdHandle(STD_OUTPUT_HANDLE);
		startupInfo.hStdError = rOptions.bCaptureOutput && rOptions.bMergeStdError ? hPipeWrite.Get() : ::GetStdHandle(STD_ERROR_HANDLE);
		PROCESS_INFORMATION processInformation {};
		std::wstring commandLine = BuildCommandLine(rArguments);
		const DWORD uiCreationFlags = (rOptions.bNoWindow ? CREATE_NO_WINDOW : 0) | (rOptions.bKillOnJobClose ? CREATE_SUSPENDED : 0);
		if (::CreateProcessW(pExecutable != nullptr ? pExecutable->c_str() : nullptr, commandLine.data(), nullptr, nullptr, TRUE, uiCreationFlags, nullptr, nullptr, &startupInfo, &processInformation) == FALSE)
		{
			ReportProcessFailure(rOptions, "launch process");
			return std::nullopt;
		}
		Handle hProcess(processInformation.hProcess);
		Handle hThread(processInformation.hThread);
		if (rOptions.bKillOnJobClose)
		{
			if (::AssignProcessToJobObject(hJob.Get(), hProcess.Get()) == FALSE)
			{
				ReportProcessFailure(rOptions, "assign process to job");
				::TerminateProcess(hProcess.Get(), ERROR_PROCESS_ABORTED);
				return std::nullopt;
			}
			if (::ResumeThread(hThread.Get()) == static_cast<DWORD>(-1))
			{
				ReportProcessFailure(rOptions, "start process");
				::TerminateProcess(hProcess.Get(), ERROR_PROCESS_ABORTED);
				return std::nullopt;
			}
		}
		hThread.Reset();
		hPipeWrite.Reset();

		ProcessResult result;
		if (rOptions.bCaptureOutput)
		{
			char pBuffer[65536] {};
			DWORD uiRead = 0;
			// A zero-byte write by the child completes ReadFile with TRUE/0; only a broken pipe is EOF.
			while (::ReadFile(hPipeRead.Get(), pBuffer, sizeof(pBuffer), &uiRead, nullptr) != FALSE)
			{
				if (uiRead == 0)
				{
					continue;
				}
				if (rOptions.outputSink)
				{
					rOptions.outputSink(pBuffer, uiRead);
				}
				else
				{
					result.output.append(pBuffer, uiRead);
				}
			}
			const DWORD uiReadError = ::GetLastError();
			if (uiReadError != ERROR_BROKEN_PIPE)
			{
				ReportProcessFailure(rOptions, "read process output");
				hPipeRead.Reset();
				::WaitForSingleObject(hProcess.Get(), INFINITE);
				return std::nullopt;
			}
		}
		if (::WaitForSingleObject(hProcess.Get(), INFINITE) != WAIT_OBJECT_0)
		{
			ReportProcessFailure(rOptions, "wait for process");
			return std::nullopt;
		}
		if (::GetExitCodeProcess(hProcess.Get(), &result.uiExitCode) == FALSE)
		{
			ReportProcessFailure(rOptions, "read process exit code");
			return std::nullopt;
		}
		return result;
	}

	std::optional<std::string> RunGit(const std::vector<std::wstring>& rArguments)
	{
		std::vector<std::wstring> arguments { L"git.exe" };
		arguments.insert(arguments.end(), rArguments.begin(), rArguments.end());
		RunProcessOptions options;
		options.bMergeStdError = true;
		options.bNoWindow = true;
		std::optional<ProcessResult> result = RunProcess(nullptr, arguments, options);
		if (!result || result->uiExitCode != 0)
		{
			return std::nullopt;
		}
		return std::move(result->output);
	}

	void SetToolName(std::string_view name)
	{
		sToolName = name;
	}

	int PrintOwnerToken()
	{
		unsigned char pBytes[16] {};
		if (::BCryptGenRandom(nullptr, pBytes, static_cast<ULONG>(sizeof(pBytes)), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
		{
			Fail("token generation failed");
			return kiExitFailure;
		}
		pBytes[6] = static_cast<unsigned char>((pBytes[6] & 0x0f) | 0x40);
		pBytes[8] = static_cast<unsigned char>((pBytes[8] & 0x3f) | 0x80);
		char pToken[37] {};
		std::snprintf(pToken, sizeof(pToken), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", pBytes[0], pBytes[1], pBytes[2], pBytes[3], pBytes[4], pBytes[5], pBytes[6], pBytes[7], pBytes[8], pBytes[9], pBytes[10], pBytes[11], pBytes[12], pBytes[13], pBytes[14], pBytes[15]);
		std::cout << pToken << '\n';
		return kiExitOk;
	}

	void Fail(std::string_view message)
	{
		std::cerr << sToolName << ": " << message << '\n';
	}

	void FailWindows(std::string_view operation)
	{
		Fail(std::string(operation) + " failed (Windows error " + std::to_string(::GetLastError()) + ")");
	}

	std::string WideToUtf8(std::wstring_view value)
	{
		if (value.empty())
		{
			return {};
		}

		int iLength = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		if (iLength <= 0)
		{
			return {};
		}

		std::string result(static_cast<size_t>(iLength), '\0');
		if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), iLength, nullptr, nullptr) != iLength)
		{
			return {};
		}
		return result;
	}

	std::wstring Utf8ToWide(std::string_view value)
	{
		if (value.empty())
		{
			return {};
		}

		int iLength = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
		if (iLength <= 0)
		{
			return {};
		}

		std::wstring result(static_cast<size_t>(iLength), L'\0');
		if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), iLength) != iLength)
		{
			return {};
		}
		return result;
	}

	std::wstring QuoteCommandLineArgument(std::wstring_view argument)
	{
		if (argument.empty())
		{
			return L"\"\"";
		}
		if (argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
		{
			return std::wstring(argument);
		}

		std::wstring result = L"\"";
		size_t uiBackslashes = 0;
		for (wchar_t cCharacter : argument)
		{
			if (cCharacter == L'\\')
			{
				++uiBackslashes;
				continue;
			}

			if (cCharacter == L'\"')
			{
				result.append(uiBackslashes * 2 + 1, L'\\');
				result.push_back(cCharacter);
				uiBackslashes = 0;
				continue;
			}

			result.append(uiBackslashes, L'\\');
			uiBackslashes = 0;
			result.push_back(cCharacter);
		}
		result.append(uiBackslashes * 2, L'\\');
		result.push_back(L'\"');
		return result;
	}

	std::wstring BuildCommandLine(const std::vector<std::wstring>& rArguments)
	{
		std::wstring commandLine;
		for (const std::wstring& rArgument : rArguments)
		{
			if (!commandLine.empty())
			{
				commandLine.push_back(L' ');
			}
			commandLine += QuoteCommandLineArgument(rArgument);
		}
		return commandLine;
	}

	std::filesystem::path GetLocalApplicationDataPath()
	{
		DWORD uiRequired = ::GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
		if (uiRequired == 0)
		{
			return {};
		}

		std::wstring value(uiRequired, L'\0');
		DWORD uiWritten = ::GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), uiRequired);
		if (uiWritten == 0 || uiWritten >= uiRequired)
		{
			return {};
		}
		value.resize(uiWritten);
		return std::filesystem::path(value);
	}

	std::filesystem::path ExtendedLengthPath(std::filesystem::path path)
	{
		// Coordination and queue-store files nest hashed directories under LOCALAPPDATA and, in deep CI/fixture
		// trees, can exceed MAX_PATH. std::filesystem forwards the path to Win32 unchanged, exactly like the raw
		// Win32 file APIs (CreateFileW/MoveFileExW/DeleteFileW) and ifstream — prefix absolute drive paths for all.
		// The raw prefix disables Win32 path normalization, so collapse redundant separators and dot components
		// first; an empty component under \\?\ fails with ERROR_INVALID_NAME.
		path.make_preferred();
		const std::wstring& rNative = path.native();
		if (rNative.size() < 2 || rNative.front() == L'\\' || !path.is_absolute())
		{
			return path;
		}
		return std::filesystem::path(L"\\\\?\\" + path.lexically_normal().native());
	}

	std::wstring ToLowerInvariant(std::wstring value)
	{
		if (!value.empty())
		{
			int iResult = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(), static_cast<int>(value.size()), value.data(), static_cast<int>(value.size()), nullptr, nullptr, 0);
			if (iResult == 0)
			{
				return {};
			}
		}
		return value;
	}

	bool EndsWithCaseInsensitive(std::wstring_view value, std::wstring_view suffix)
	{
		if (value.size() < suffix.size())
		{
			return false;
		}
		std::wstring valueEnd(value.substr(value.size() - suffix.size()));
		return ToLowerInvariant(std::move(valueEnd)) == ToLowerInvariant(std::wstring(suffix));
	}
}
