#include "BuildCommand.h"

#include "ToolCliCommon.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "tinygltf/json.hpp"

namespace toolcli
{
	namespace
	{
		constexpr int64_t kiBuildLockWaitSeconds = 660;

		struct ProcessResult
		{
			DWORD uiExitCode = ERROR_GEN_FAILURE;
			std::string output;
		};

		std::optional<ProcessResult> RunProcess(const std::filesystem::path& rExecutable, const std::vector<std::wstring>& rArguments, bool bCaptureOutput)
		{
			Handle hJob(::CreateJobObjectW(nullptr, nullptr));
			if (!hJob.IsValid())
			{
				FailWindows("create build job");
				return std::nullopt;
			}
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInformation {};
			jobInformation.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			if (::SetInformationJobObject(hJob.Get(), JobObjectExtendedLimitInformation, &jobInformation, sizeof(jobInformation)) == FALSE)
			{
				FailWindows("configure build job");
				return std::nullopt;
			}

			SECURITY_ATTRIBUTES pipeAttributes {};
			pipeAttributes.nLength = sizeof(pipeAttributes);
			pipeAttributes.bInheritHandle = TRUE;
			Handle hPipeRead;
			Handle hPipeWrite;
			if (bCaptureOutput)
			{
				HANDLE hRead = INVALID_HANDLE_VALUE;
				HANDLE hWrite = INVALID_HANDLE_VALUE;
				if (::CreatePipe(&hRead, &hWrite, &pipeAttributes, 0) == FALSE)
				{
					FailWindows("create output pipe");
					return std::nullopt;
				}
				hPipeRead.Reset(hRead);
				hPipeWrite.Reset(hWrite);
				if (::SetHandleInformation(hPipeRead.Get(), HANDLE_FLAG_INHERIT, 0) == FALSE)
				{
					FailWindows("configure output pipe");
					return std::nullopt;
				}
			}

			STARTUPINFOW startupInfo {};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESTDHANDLES;
			startupInfo.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
			startupInfo.hStdOutput = bCaptureOutput ? hPipeWrite.Get() : ::GetStdHandle(STD_OUTPUT_HANDLE);
			startupInfo.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
			PROCESS_INFORMATION processInformation {};
			std::wstring commandLine = BuildCommandLine(rArguments);
			if (::CreateProcessW(rExecutable.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &startupInfo, &processInformation) == FALSE)
			{
				FailWindows("launch process");
				return std::nullopt;
			}
			Handle hProcess(processInformation.hProcess);
			Handle hThread(processInformation.hThread);
			if (::AssignProcessToJobObject(hJob.Get(), hProcess.Get()) == FALSE)
			{
				FailWindows("assign process to build job");
				::TerminateProcess(hProcess.Get(), ERROR_PROCESS_ABORTED);
				return std::nullopt;
			}
			if (::ResumeThread(hThread.Get()) == static_cast<DWORD>(-1))
			{
				FailWindows("start process");
				::TerminateProcess(hProcess.Get(), ERROR_PROCESS_ABORTED);
				return std::nullopt;
			}
			hThread.Reset();
			hPipeWrite.Reset();

			ProcessResult result;
			if (bCaptureOutput)
			{
				char pBuffer[4096] {};
				DWORD uiRead = 0;
				while (::ReadFile(hPipeRead.Get(), pBuffer, sizeof(pBuffer), &uiRead, nullptr) != FALSE && uiRead > 0)
				{
					result.output.append(pBuffer, uiRead);
				}
			}
			::WaitForSingleObject(hProcess.Get(), INFINITE);
			if (::GetExitCodeProcess(hProcess.Get(), &result.uiExitCode) == FALSE)
			{
				FailWindows("read process exit code");
				return std::nullopt;
			}
			return result;
		}

		std::optional<std::filesystem::path> FindMsBuild()
		{
			std::filesystem::path defaultPath = L"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe";
			if (std::filesystem::is_regular_file(defaultPath))
			{
				return defaultPath;
			}

			DWORD uiRequired = ::GetEnvironmentVariableW(L"ProgramFiles(x86)", nullptr, 0);
			if (uiRequired == 0)
			{
				return std::nullopt;
			}
			std::wstring programFiles(uiRequired, L'\0');
			DWORD uiWritten = ::GetEnvironmentVariableW(L"ProgramFiles(x86)", programFiles.data(), uiRequired);
			if (uiWritten == 0 || uiWritten >= uiRequired)
			{
				return std::nullopt;
			}
			programFiles.resize(uiWritten);
			std::filesystem::path vswherePath = std::filesystem::path(programFiles) / L"Microsoft Visual Studio" / L"Installer" / L"vswhere.exe";
			if (!std::filesystem::is_regular_file(vswherePath))
			{
				return std::nullopt;
			}

			std::vector<std::wstring> arguments = { vswherePath.native(), L"-latest", L"-products", L"*", L"-requires", L"Microsoft.Component.MSBuild", L"-find", L"MSBuild\\**\\Bin\\MSBuild.exe" };
			std::optional<ProcessResult> result = RunProcess(vswherePath, arguments, true);
			if (!result || result->uiExitCode != 0)
			{
				return std::nullopt;
			}
			std::string firstLine = result->output.substr(0, result->output.find_first_of("\r\n"));
			std::filesystem::path msbuildPath = Utf8ToWide(firstLine);
			if (!std::filesystem::is_regular_file(msbuildPath))
			{
				return std::nullopt;
			}
			return msbuildPath;
		}

		std::optional<std::filesystem::path> FindWorktreeRoot(const std::filesystem::path& rTarget)
		{
			std::error_code error;
			std::filesystem::path current = std::filesystem::absolute(rTarget, error).parent_path();
			if (error)
			{
				return std::nullopt;
			}
			while (!current.empty())
			{
				if (std::filesystem::exists(current / L".git", error) && !error)
				{
					return current;
				}
				error.clear();
				std::filesystem::path parent = current.parent_path();
				if (parent == current)
				{
					break;
				}
				current = std::move(parent);
			}
			return std::nullopt;
		}

		std::optional<Handle> AcquireBuildLock(const std::filesystem::path& rWorktreeRoot, const std::filesystem::path& rTarget)
		{
			std::filesystem::path lockDirectory = rWorktreeRoot / L".claude" / L"build-locks";
			std::error_code error;
			std::filesystem::create_directories(lockDirectory, error);
			if (error)
			{
				Fail("could not create build lock directory");
				return std::nullopt;
			}

			std::wstring lockName = ToLowerInvariant(rTarget.stem().native()) + L".lock";
			std::filesystem::path lockPath = lockDirectory / lockName;
			for (int64_t iElapsedSeconds = 0; iElapsedSeconds <= kiBuildLockWaitSeconds; iElapsedSeconds += 5)
			{
				Handle hLock(::CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr));
				if (hLock.IsValid())
				{
					std::string owner = std::to_string(::GetCurrentProcessId()) + "\n";
					::SetFilePointer(hLock.Get(), 0, nullptr, FILE_BEGIN);
					::SetEndOfFile(hLock.Get());
					DWORD uiWritten = 0;
					::WriteFile(hLock.Get(), owner.data(), static_cast<DWORD>(owner.size()), &uiWritten, nullptr);
					return hLock;
				}
				if (::GetLastError() != ERROR_SHARING_VIOLATION && ::GetLastError() != ERROR_LOCK_VIOLATION)
				{
					FailWindows("acquire build lock");
					return std::nullopt;
				}
				if (iElapsedSeconds == kiBuildLockWaitSeconds)
				{
					break;
				}
				std::wcerr << L"WorktreeCli: waiting for build lock on " << rTarget.stem().native() << L" (" << iElapsedSeconds << L"s elapsed)\n";
				std::this_thread::sleep_for(std::chrono::seconds(5));
			}
			Fail("timed out waiting for build lock after 660 seconds");
			return std::nullopt;
		}

		bool HasRequiredProperty(const std::vector<std::wstring>& rArguments, std::wstring_view propertyName)
		{
			std::wstring shortSlashPrefix = ToLowerInvariant(L"/p:" + std::wstring(propertyName) + L"=");
			std::wstring shortDashPrefix = ToLowerInvariant(L"-p:" + std::wstring(propertyName) + L"=");
			std::wstring longSlashPrefix = ToLowerInvariant(L"/property:" + std::wstring(propertyName) + L"=");
			std::wstring longDashPrefix = ToLowerInvariant(L"-property:" + std::wstring(propertyName) + L"=");
			for (const std::wstring& rArgument : rArguments)
			{
				std::wstring lower = ToLowerInvariant(rArgument);
				if (lower.starts_with(shortSlashPrefix) || lower.starts_with(shortDashPrefix) || lower.starts_with(longSlashPrefix) || lower.starts_with(longDashPrefix))
				{
					return true;
				}
			}
			return false;
		}

		std::wstring ComparablePath(const std::filesystem::path& rPath)
		{
			std::error_code error;
			std::filesystem::path absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(rPath, error), error);
			if (error)
			{
				return {};
			}
			std::wstring result = absolute.native();
			std::replace(result.begin(), result.end(), L'/', L'\\');
			return ToLowerInvariant(std::move(result));
		}

		bool InvalidateSelectedObjects(const std::filesystem::path& rMsBuild, const std::filesystem::path& rProject, const std::vector<std::wstring>& rBuildArguments, const std::vector<std::filesystem::path>& rSelectedFiles)
		{
			if (!HasRequiredProperty(rBuildArguments, L"Configuration") || !HasRequiredProperty(rBuildArguments, L"Platform"))
			{
				Fail("--files requires Configuration and Platform properties");
				return false;
			}

			std::vector<std::wstring> queryArguments = { rMsBuild.native(), rProject.native() };
			queryArguments.insert(queryArguments.end(), rBuildArguments.begin(), rBuildArguments.end());
			queryArguments.emplace_back(L"/getProperty:IntDir");
			queryArguments.emplace_back(L"/getItem:ClCompile");
			queryArguments.emplace_back(L"/nologo");
			std::optional<ProcessResult> queryResult = RunProcess(rMsBuild, queryArguments, true);
			if (!queryResult || queryResult->uiExitCode != 0)
			{
				Fail("MSBuild project evaluation failed");
				return false;
			}

			struct BuildItem
			{
				std::filesystem::path source;
				std::filesystem::path object;
			};
			std::filesystem::path intermediateDirectory;
			std::wstring comparableIntermediateDirectory;
			std::unordered_map<std::wstring, BuildItem> items;
			try
			{
				nlohmann::json evaluation = nlohmann::json::parse(queryResult->output);
				if (!evaluation.is_object() || !evaluation.contains("Properties") || !evaluation["Properties"].is_object() ||
					!evaluation["Properties"].contains("IntDir") || !evaluation["Properties"]["IntDir"].is_string() ||
					!evaluation.contains("Items") || !evaluation["Items"].is_object() ||
					!evaluation["Items"].contains("ClCompile") || !evaluation["Items"]["ClCompile"].is_array())
				{
					Fail("MSBuild evaluation omitted or malformed IntDir or ClCompile");
					return false;
				}

				std::wstring intermediateDirectoryValue = Utf8ToWide(evaluation["Properties"]["IntDir"].get<std::string>());
				if (intermediateDirectoryValue.empty())
				{
					Fail("invalid evaluated IntDir");
					return false;
				}
				intermediateDirectory = intermediateDirectoryValue;
				comparableIntermediateDirectory = ComparablePath(intermediateDirectory);
				if (comparableIntermediateDirectory.empty())
				{
					Fail("invalid evaluated IntDir");
					return false;
				}
				if (comparableIntermediateDirectory.back() != L'\\')
				{
					comparableIntermediateDirectory.push_back(L'\\');
				}

				for (const nlohmann::json& rItem : evaluation["Items"]["ClCompile"])
				{
					if (!rItem.is_object() || !rItem.contains("Identity") || !rItem["Identity"].is_string())
					{
						continue;
					}
					std::filesystem::path source = Utf8ToWide(rItem["Identity"].get<std::string>());
					if (source.is_relative())
					{
						source = rProject.parent_path() / source;
					}
					std::filesystem::path object = intermediateDirectory;
					if (rItem.contains("ObjectFileName") && rItem["ObjectFileName"].is_string() && !rItem["ObjectFileName"].get<std::string>().empty())
					{
						object = Utf8ToWide(rItem["ObjectFileName"].get<std::string>());
						if (object.is_relative())
						{
							object = rProject.parent_path() / object;
						}
					}
					if (!EndsWithCaseInsensitive(object.native(), L".obj"))
					{
						object /= source.stem().native() + L".obj";
					}
					items.emplace(ComparablePath(source), BuildItem
					{
						.source = source,
						.object = object,
					});
				}
			}
			catch (const std::exception& rException)
			{
				Fail(std::string("could not read MSBuild evaluation: ") + rException.what());
				return false;
			}

			for (const std::filesystem::path& rSelectedFile : rSelectedFiles)
			{
				if (!EndsWithCaseInsensitive(rSelectedFile.native(), L".cpp"))
				{
					Fail("--files only accepts .cpp inputs");
					return false;
				}
				std::wstring comparableSource = ComparablePath(rSelectedFile);
				auto it = items.find(comparableSource);
				if (comparableSource.empty() || it == items.end())
				{
					Fail("selected .cpp is not a ClCompile member: " + WideToUtf8(rSelectedFile.native()));
					return false;
				}
				std::wstring comparableObject = ComparablePath(it->second.object);
				if (comparableObject.empty() || !comparableObject.starts_with(comparableIntermediateDirectory))
				{
					Fail("evaluated object path escapes IntDir");
					return false;
				}
				if (std::filesystem::exists(it->second.object) && ::DeleteFileW(it->second.object.c_str()) == FALSE)
				{
					FailWindows("delete selected object");
					return false;
				}
				std::wcout << L"WorktreeCli: invalidated " << it->second.object.native() << L'\n';
			}
			return true;
		}
	}

	int RunBuildCommand(int iArgumentCount, wchar_t* pArgumentValues[])
	{
		std::vector<std::filesystem::path> selectedFiles;
		int iIndex = 2;
		if (iIndex < iArgumentCount && std::wstring_view(pArgumentValues[iIndex]) == L"--files")
		{
			++iIndex;
			while (iIndex < iArgumentCount && std::wstring_view(pArgumentValues[iIndex]) != L"--")
			{
				selectedFiles.emplace_back(pArgumentValues[iIndex++]);
			}
			if (selectedFiles.empty() || iIndex >= iArgumentCount || std::wstring_view(pArgumentValues[iIndex]) != L"--")
			{
				Fail("--files requires one or more .cpp paths followed by --");
				return kiExitFailure;
			}
			++iIndex;
		}
		if (iIndex >= iArgumentCount)
		{
			Fail("build requires a project or solution path");
			return kiExitFailure;
		}

		std::filesystem::path target = pArgumentValues[iIndex++];
		std::vector<std::wstring> buildArguments;
		for (; iIndex < iArgumentCount; ++iIndex)
		{
			buildArguments.emplace_back(pArgumentValues[iIndex]);
		}
		if (!selectedFiles.empty() && !EndsWithCaseInsensitive(target.native(), L".vcxproj"))
		{
			Fail("--files requires a .vcxproj target");
			return kiExitFailure;
		}
		if (!std::filesystem::is_regular_file(target))
		{
			Fail("build target does not exist");
			return kiExitFailure;
		}

		std::optional<std::filesystem::path> worktreeRoot = FindWorktreeRoot(target);
		std::optional<std::filesystem::path> msbuildPath = FindMsBuild();
		if (!worktreeRoot || !msbuildPath)
		{
			Fail("could not locate worktree root or MSBuild");
			return kiExitFailure;
		}
		std::optional<Handle> buildLock = AcquireBuildLock(*worktreeRoot, target);
		if (!buildLock)
		{
			return kiExitFailure;
		}

		if (!selectedFiles.empty() && !InvalidateSelectedObjects(*msbuildPath, target, buildArguments, selectedFiles))
		{
			return kiExitFailure;
		}

		std::vector<std::wstring> arguments = { msbuildPath->native(), target.native() };
		arguments.insert(arguments.end(), buildArguments.begin(), buildArguments.end());
		std::wcout << L"WorktreeCli: building " << target.native() << L'\n' << std::flush;
		std::optional<ProcessResult> result = RunProcess(*msbuildPath, arguments, false);
		if (!result)
		{
			return kiExitFailure;
		}
		return static_cast<int>(result->uiExitCode);
	}
}
