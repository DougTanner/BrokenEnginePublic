#include "BuildCommand.h"

#include "CoordinationStore.h"
#include "ToolCliCommon.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "tinygltf/json.hpp"

namespace toolcli
{
	namespace
	{
		constexpr int64_t kiBuildLockWaitSeconds = 660;
		constexpr size_t kuiMaxDiagnostics = 500;
		constexpr size_t kuiMaxUnmatchedMessages = 50;
		// Backtracking in the diagnostic regexes is superlinear; pathological MSVC template
		// diagnostics can span tens of KB on one line, so oversized lines skip parsing (the
		// retained log still holds them verbatim).
		constexpr size_t kuiMaxDiagnosticLineLength = 4096;

		// The build command's public contract is exactly one of these objects on stdout.
		constexpr std::string_view kBuildResultSchema = "broken-engine-build-result/v1";

		std::vector<std::string> sBuildMessages;

		std::optional<std::wstring> GetEnvironmentValue(const wchar_t* pName)
		{
			DWORD uiRequired = ::GetEnvironmentVariableW(pName, nullptr, 0);
			if (uiRequired == 0)
			{
				return std::nullopt;
			}
			std::wstring value(uiRequired, L'\0');
			DWORD uiWritten = ::GetEnvironmentVariableW(pName, value.data(), uiRequired);
			if (uiWritten == 0 || uiWritten >= uiRequired)
			{
				return std::nullopt;
			}
			value.resize(uiWritten);
			return value;
		}

		// Fixtures may shorten (never lengthen) the lock wait through BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS.
		int64_t GetBuildLockWaitSeconds()
		{
			std::optional<std::wstring> override = GetEnvironmentValue(L"BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS");
			if (override)
			{
				wchar_t* pEnd = nullptr;
				int64_t iSeconds = std::wcstol(override->c_str(), &pEnd, 10);
				if (pEnd != override->c_str() && *pEnd == L'\0' && iSeconds >= 0 && iSeconds < kiBuildLockWaitSeconds)
				{
					return iSeconds;
				}
			}
			return kiBuildLockWaitSeconds;
		}

		void FailBuild(std::string_view message)
		{
			Fail(message);
			sBuildMessages.emplace_back(message);
		}

		void FailBuildWindows(std::string_view operation)
		{
			FailBuild(std::string(operation) + " failed (Windows error " + std::to_string(::GetLastError()) + ")");
		}

		std::optional<ProcessResult> RunBuildProcess(const std::filesystem::path& rExecutable, const std::vector<std::wstring>& rArguments, bool bCaptureOutput)
		{
			RunProcessOptions options;
			options.bCaptureOutput = bCaptureOutput;
			options.bKillOnJobClose = true;
			options.bReportFailures = true;
			return RunProcess(&rExecutable, rArguments, options);
		}

		struct JobProcess
		{
			Handle hJob;
			Handle hProcess;
		};

		std::optional<JobProcess> LaunchJobProcess(const std::filesystem::path& rExecutable, const std::vector<std::wstring>& rArguments, HANDLE hStdOutput, HANDLE hStdError)
		{
			Handle hJob(::CreateJobObjectW(nullptr, nullptr));
			if (!hJob.IsValid())
			{
				FailBuildWindows("create build job");
				return std::nullopt;
			}
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInformation {};
			jobInformation.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			if (::SetInformationJobObject(hJob.Get(), JobObjectExtendedLimitInformation, &jobInformation, sizeof(jobInformation)) == FALSE)
			{
				FailBuildWindows("configure build job");
				return std::nullopt;
			}

			STARTUPINFOW startupInfo {};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESTDHANDLES;
			startupInfo.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
			startupInfo.hStdOutput = hStdOutput;
			startupInfo.hStdError = hStdError;
			PROCESS_INFORMATION processInformation {};
			std::wstring commandLine = BuildCommandLine(rArguments);
			if (::CreateProcessW(rExecutable.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &startupInfo, &processInformation) == FALSE)
			{
				FailBuildWindows("launch process");
				return std::nullopt;
			}
			Handle hProcess(processInformation.hProcess);
			Handle hThread(processInformation.hThread);
			if (::AssignProcessToJobObject(hJob.Get(), hProcess.Get()) == FALSE)
			{
				FailBuildWindows("assign process to build job");
				::TerminateProcess(hProcess.Get(), ERROR_PROCESS_ABORTED);
				return std::nullopt;
			}
			if (::ResumeThread(hThread.Get()) == static_cast<DWORD>(-1))
			{
				FailBuildWindows("start process");
				::TerminateProcess(hProcess.Get(), ERROR_PROCESS_ABORTED);
				return std::nullopt;
			}
			return JobProcess
			{
				.hJob = std::move(hJob),
				.hProcess = std::move(hProcess),
			};
		}

		bool CreateOutputPipe(Handle& rhPipeRead, Handle& rhPipeWrite)
		{
			SECURITY_ATTRIBUTES pipeAttributes {};
			pipeAttributes.nLength = sizeof(pipeAttributes);
			pipeAttributes.bInheritHandle = TRUE;
			HANDLE hRead = INVALID_HANDLE_VALUE;
			HANDLE hWrite = INVALID_HANDLE_VALUE;
			if (::CreatePipe(&hRead, &hWrite, &pipeAttributes, 0) == FALSE)
			{
				FailBuildWindows("create output pipe");
				return false;
			}
			rhPipeRead.Reset(hRead);
			rhPipeWrite.Reset(hWrite);
			if (::SetHandleInformation(rhPipeRead.Get(), HANDLE_FLAG_INHERIT, 0) == FALSE)
			{
				FailBuildWindows("configure output pipe");
				return false;
			}
			return true;
		}

		class RetainedLog
		{
		public:
			bool Open(const std::filesystem::path& rDirectory, const std::wstring& rTargetStem)
			{
				std::error_code error;
				std::filesystem::create_directories(rDirectory, error);
				if (error)
				{
					FailBuild("could not create retained build log directory");
					return false;
				}

				SYSTEMTIME time {};
				::GetSystemTime(&time);
				wchar_t pTimestamp[32] {};
				std::swprintf(pTimestamp, std::size(pTimestamp), L"%04u%02u%02uT%02u%02u%02u%03uZ", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
				std::wstring baseName = ToLowerInvariant(rTargetStem) + L"-" + pTimestamp + L"-" + std::to_wstring(::GetCurrentProcessId());
				for (int iAttempt = 0; iAttempt < 16; ++iAttempt)
				{
					std::wstring name = iAttempt == 0 ? baseName + L".log" : baseName + L"-" + std::to_wstring(iAttempt) + L".log";
					std::filesystem::path candidate = rDirectory / name;
					Handle hFile(::CreateFileW(candidate.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
					if (hFile.IsValid())
					{
						mhFile = std::move(hFile);
						mPath = std::move(candidate);
						return true;
					}
					if (::GetLastError() != ERROR_FILE_EXISTS)
					{
						FailBuildWindows("create retained build log");
						return false;
					}
				}
				FailBuild("could not create a collision-free retained build log name");
				return false;
			}

			void Write(const char* pData, size_t uiSize)
			{
				if (mbFailed)
				{
					return;
				}
				while (uiSize > 0)
				{
					DWORD uiChunk = static_cast<DWORD>(std::min<size_t>(uiSize, 1u << 20));
					DWORD uiWritten = 0;
					if (::WriteFile(mhFile.Get(), pData, uiChunk, &uiWritten, nullptr) == FALSE || uiWritten == 0)
					{
						mbFailed = true;
						FailBuildWindows("write retained build log");
						return;
					}
					pData += uiWritten;
					uiSize -= uiWritten;
					muiBytes += uiWritten;
				}
			}

			[[nodiscard]] const std::filesystem::path& GetPath() const
			{
				return mPath;
			}

			[[nodiscard]] uint64_t GetBytes() const
			{
				return muiBytes;
			}

			[[nodiscard]] bool HasFailure() const
			{
				return mbFailed;
			}

		private:
			Handle mhFile;
			std::filesystem::path mPath;
			uint64_t muiBytes = 0;
			bool mbFailed = false;
		};

		class DiagnosticParser
		{
		public:
			DiagnosticParser() :
				mOriginDiagnostic(R"(^\s*(.+?)(?:\((\d+)(?:,(\d+))?\))?\s*:\s*(?:[A-Za-z][A-Za-z ]*\s+)?(error|warning)\s+([A-Za-z]+\d+)\s*:\s*(.*?)\s*(?:\[([^\][]*)\])?\s*$)"),
				mBareDiagnostic(R"(^\s*(?:[A-Za-z][A-Za-z ]*\s+)?(error|warning)\s+([A-Za-z]+\d+)\s*:\s*(.*)$)")
			{
			}

			void Consume(const char* pData, size_t uiSize)
			{
				mCarry.append(pData, uiSize);
				size_t uiStart = 0;
				for (size_t uiIndex = mCarry.find('\n', 0); uiIndex != std::string::npos; uiIndex = mCarry.find('\n', uiStart))
				{
					ParseLine(std::string_view(mCarry).substr(uiStart, uiIndex - uiStart));
					uiStart = uiIndex + 1;
				}
				mCarry.erase(0, uiStart);
			}

			void Finish()
			{
				if (!mCarry.empty())
				{
					ParseLine(mCarry);
					mCarry.clear();
				}
			}

			[[nodiscard]] const nlohmann::json& GetDiagnostics() const
			{
				return mDiagnostics;
			}

			[[nodiscard]] bool WereDiagnosticsTruncated() const
			{
				return mbTruncated;
			}

		private:
			static int ParseNumber(const std::csub_match& rMatch)
			{
				if (!rMatch.matched)
				{
					return 0;
				}
				int64_t iValue = std::strtoll(rMatch.first, nullptr, 10);
				return iValue > 0 && iValue <= (std::numeric_limits<int>::max)() ? static_cast<int>(iValue) : 0;
			}

			void ParseLine(std::string_view line)
			{
				while (!line.empty() && (line.back() == '\r' || line.back() == '\0'))
				{
					line.remove_suffix(1);
				}
				if (line.empty() || line.size() > kuiMaxDiagnosticLineLength ||
					(line.find("error") == std::string_view::npos && line.find("warning") == std::string_view::npos))
				{
					return;
				}

				try
				{
					std::cmatch match;
					if (std::regex_match(line.data(), line.data() + line.size(), match, mOriginDiagnostic))
					{
						AppendDiagnostic(match[4].str(), match[5].str(), match[1].str(), ParseNumber(match[2]), ParseNumber(match[3]), match[7].str(), match[6].str(), line);
						return;
					}
					if (std::regex_match(line.data(), line.data() + line.size(), match, mBareDiagnostic))
					{
						AppendDiagnostic(match[1].str(), match[2].str(), {}, 0, 0, {}, match[3].str(), line);
						return;
					}
				}
				catch (const std::regex_error&)
				{
					// Backtracking limit hit; fall through to the unmatched-line handling.
				}
				if (line.find("error") != std::string_view::npos && sBuildMessages.size() < kuiMaxUnmatchedMessages)
				{
					sBuildMessages.emplace_back(line);
				}
			}

			void AppendDiagnostic(std::string severity, std::string code, std::string file, int iLine, int iColumn, std::string project, std::string message, std::string_view raw)
			{
				// MSBuild repeats each diagnostic in its end-of-build summary; keep one entry per identity.
				std::string key = severity + '|' + code + '|' + file + '|' + std::to_string(iLine) + '|' + std::to_string(iColumn) + '|' + project + '|' + message;
				if (!mSeenDiagnostics.insert(std::move(key)).second)
				{
					return;
				}
				if (mDiagnostics.size() >= kuiMaxDiagnostics)
				{
					mbTruncated = true;
					return;
				}
				nlohmann::json diagnostic;
				diagnostic["severity"] = std::move(severity);
				diagnostic["code"] = std::move(code);
				diagnostic["file"] = file.empty() ? nlohmann::json() : nlohmann::json(std::move(file));
				diagnostic["line"] = iLine > 0 ? nlohmann::json(iLine) : nlohmann::json();
				diagnostic["column"] = iColumn > 0 ? nlohmann::json(iColumn) : nlohmann::json();
				diagnostic["project"] = project.empty() ? nlohmann::json() : nlohmann::json(std::move(project));
				diagnostic["message"] = std::move(message);
				diagnostic["raw"] = std::string(raw);
				mDiagnostics.push_back(std::move(diagnostic));
			}

			std::regex mOriginDiagnostic;
			std::regex mBareDiagnostic;
			std::string mCarry;
			std::unordered_set<std::string> mSeenDiagnostics;
			nlohmann::json mDiagnostics = nlohmann::json::array();
			bool mbTruncated = false;
		};

		// Launches MSBuild with stdout and stderr bound to one pipe so the retained log
		// preserves the observed read order of the combined stream.
		std::optional<DWORD> RunMsBuildToLog(const std::filesystem::path& rExecutable, const std::vector<std::wstring>& rArguments, RetainedLog& rLog, DiagnosticParser& rParser)
		{
			Handle hPipeRead;
			Handle hPipeWrite;
			if (!CreateOutputPipe(hPipeRead, hPipeWrite))
			{
				return std::nullopt;
			}

			std::optional<JobProcess> process = LaunchJobProcess(rExecutable, rArguments, hPipeWrite.Get(), hPipeWrite.Get());
			if (!process)
			{
				return std::nullopt;
			}
			hPipeWrite.Reset();

			char pBuffer[65536] {};
			DWORD uiRead = 0;
			// A zero-byte write by the child completes ReadFile with TRUE/0; only a broken pipe is EOF.
			while (::ReadFile(hPipeRead.Get(), pBuffer, sizeof(pBuffer), &uiRead, nullptr) != FALSE)
			{
				if (uiRead == 0)
				{
					continue;
				}
				rLog.Write(pBuffer, uiRead);
				rParser.Consume(pBuffer, uiRead);
			}
			rParser.Finish();

			::WaitForSingleObject(process->hProcess.Get(), INFINITE);
			DWORD uiExitCode = ERROR_GEN_FAILURE;
			if (::GetExitCodeProcess(process->hProcess.Get(), &uiExitCode) == FALSE)
			{
				FailBuildWindows("read process exit code");
				return std::nullopt;
			}
			return uiExitCode;
		}

		std::optional<std::filesystem::path> FindMsBuild()
		{
			// An explicit pin must resolve; a broken pin fails discovery instead of silently falling back.
			std::optional<std::wstring> pinnedPath = GetEnvironmentValue(L"BROKEN_ENGINE_MSBUILD_PATH");
			if (pinnedPath)
			{
				std::filesystem::path pinned = *pinnedPath;
				if (std::filesystem::is_regular_file(pinned))
				{
					return pinned;
				}
				return std::nullopt;
			}

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
			std::optional<ProcessResult> result = RunBuildProcess(vswherePath, arguments, true);
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

		std::optional<Handle> AcquireBuildLock(const std::filesystem::path& rWorktreeRoot, const std::filesystem::path& rTarget, std::filesystem::path& rLockPath, int64_t& riWaitedSeconds, const char*& rpDisposition)
		{
			rpDisposition = "failed";
			std::filesystem::path lockDirectory = rWorktreeRoot / L".claude" / L"build-locks";
			std::error_code error;
			std::filesystem::create_directories(lockDirectory, error);
			if (error)
			{
				FailBuild("could not create build lock directory");
				return std::nullopt;
			}

			std::wstring lockName = ToLowerInvariant(rTarget.stem().native()) + L".lock";
			rLockPath = lockDirectory / lockName;
			int64_t iWaitSeconds = GetBuildLockWaitSeconds();
			for (int64_t iElapsedSeconds = 0; iElapsedSeconds <= iWaitSeconds; iElapsedSeconds += 5)
			{
				riWaitedSeconds = iElapsedSeconds;
				Handle hLock(::CreateFileW(rLockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr));
				if (hLock.IsValid())
				{
					std::string owner = std::to_string(::GetCurrentProcessId()) + "\n";
					::SetFilePointer(hLock.Get(), 0, nullptr, FILE_BEGIN);
					::SetEndOfFile(hLock.Get());
					DWORD uiWritten = 0;
					::WriteFile(hLock.Get(), owner.data(), static_cast<DWORD>(owner.size()), &uiWritten, nullptr);
					rpDisposition = "acquired";
					return hLock;
				}
				if (::GetLastError() != ERROR_SHARING_VIOLATION && ::GetLastError() != ERROR_LOCK_VIOLATION)
				{
					FailBuildWindows("acquire build lock");
					return std::nullopt;
				}
				if (iElapsedSeconds == iWaitSeconds)
				{
					break;
				}
				std::wcerr << L"WorktreeCli: waiting for build lock on " << rTarget.stem().native() << L" (" << iElapsedSeconds << L"s elapsed)\n";
				std::this_thread::sleep_for(std::chrono::seconds(5));
			}
			rpDisposition = "timeout";
			FailBuild("timed out waiting for build lock after " + std::to_string(iWaitSeconds) + " seconds");
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

		bool InvalidateSelectedObjects(const std::filesystem::path& rMsBuild, const std::filesystem::path& rProject, const std::vector<std::wstring>& rBuildArguments, const std::vector<std::filesystem::path>& rSelectedFiles, RetainedLog& rLog, nlohmann::json& rInvalidatedObjects)
		{
			if (!HasRequiredProperty(rBuildArguments, L"Configuration") || !HasRequiredProperty(rBuildArguments, L"Platform"))
			{
				FailBuild("--files requires Configuration and Platform properties");
				return false;
			}

			std::vector<std::wstring> queryArguments = { rMsBuild.native(), rProject.native() };
			queryArguments.insert(queryArguments.end(), rBuildArguments.begin(), rBuildArguments.end());
			queryArguments.emplace_back(L"/getProperty:IntDir");
			queryArguments.emplace_back(L"/getItem:ClCompile");
			queryArguments.emplace_back(L"/nologo");
			std::optional<ProcessResult> queryResult = RunBuildProcess(rMsBuild, queryArguments, true);
			if (queryResult)
			{
				rLog.Write(queryResult->output.data(), queryResult->output.size());
			}
			if (!queryResult || queryResult->uiExitCode != 0)
			{
				FailBuild("MSBuild project evaluation failed");
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
					FailBuild("MSBuild evaluation omitted or malformed IntDir or ClCompile");
					return false;
				}

				std::wstring intermediateDirectoryValue = Utf8ToWide(evaluation["Properties"]["IntDir"].get<std::string>());
				if (intermediateDirectoryValue.empty())
				{
					FailBuild("invalid evaluated IntDir");
					return false;
				}
				intermediateDirectory = intermediateDirectoryValue;
				comparableIntermediateDirectory = ComparablePath(intermediateDirectory);
				if (comparableIntermediateDirectory.empty())
				{
					FailBuild("invalid evaluated IntDir");
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
				FailBuild(std::string("could not read MSBuild evaluation: ") + rException.what());
				return false;
			}

			for (const std::filesystem::path& rSelectedFile : rSelectedFiles)
			{
				if (!EndsWithCaseInsensitive(rSelectedFile.native(), L".cpp"))
				{
					FailBuild("--files only accepts .cpp inputs");
					return false;
				}
				std::wstring comparableSource = ComparablePath(rSelectedFile);
				auto it = items.find(comparableSource);
				if (comparableSource.empty() || it == items.end())
				{
					FailBuild("selected .cpp is not a ClCompile member: " + WideToUtf8(rSelectedFile.native()));
					return false;
				}
				std::wstring comparableObject = ComparablePath(it->second.object);
				if (comparableObject.empty() || !comparableObject.starts_with(comparableIntermediateDirectory))
				{
					FailBuild("evaluated object path escapes IntDir");
					return false;
				}
				if (std::filesystem::exists(it->second.object) && ::DeleteFileW(it->second.object.c_str()) == FALSE)
				{
					FailBuildWindows("delete selected object");
					return false;
				}
				std::wcerr << L"WorktreeCli: invalidated " << it->second.object.native() << L'\n';
				rInvalidatedObjects.push_back(WideToUtf8(it->second.object.native()));
			}
			return true;
		}
	}

	static int RunBuildCommandUnguarded(int iArgumentCount, wchar_t* pArgumentValues[])
	{
		std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		sBuildMessages.clear();

		nlohmann::json result;
		result["schemaVersion"] = kBuildResultSchema;
		result["status"] = "fail";
		result["failureKind"] = "tool";
		result["startedAt"] = coordination::CurrentUtcTimestamp();
		result["target"] = nullptr;
		result["worktreeRoot"] = nullptr;
		result["arguments"] = nlohmann::json::array();
		result["selectedFiles"] = nlohmann::json::array();
		result["invalidatedObjects"] = nlohmann::json::array();
		result["lock"] = { { "disposition", "not-attempted" }, { "path", nullptr }, { "waitedSeconds", 0 } };
		result["msbuild"] = { { "discovered", false }, { "path", nullptr }, { "launched", false }, { "exitCode", nullptr } };
		result["retainedLog"] = nullptr;
		result["diagnostics"] = nlohmann::json::array();
		result["diagnosticsTruncated"] = false;

		// Every path out of this command emits exactly one schema-versioned JSON result on stdout.
		const auto EmitResult = [&result, start](int iExitCode) -> int
		{
			result["exitCode"] = iExitCode;
			result["elapsedMilliseconds"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			result["messages"] = sBuildMessages;
			std::cout << result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
			return iExitCode;
		};

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
				FailBuild("--files requires one or more .cpp paths followed by --");
				return EmitResult(kiExitFailure);
			}
			++iIndex;
		}
		if (iIndex >= iArgumentCount)
		{
			FailBuild("build requires a project or solution path");
			return EmitResult(kiExitFailure);
		}

		std::filesystem::path target = pArgumentValues[iIndex++];
		std::vector<std::wstring> buildArguments;
		for (; iIndex < iArgumentCount; ++iIndex)
		{
			buildArguments.emplace_back(pArgumentValues[iIndex]);
		}

		result["target"] = { { "requested", WideToUtf8(target.native()) }, { "normalized", WideToUtf8(ComparablePath(target)) } };
		for (const std::wstring& rArgument : buildArguments)
		{
			result["arguments"].push_back(WideToUtf8(rArgument));
		}
		for (const std::filesystem::path& rSelectedFile : selectedFiles)
		{
			result["selectedFiles"].push_back(WideToUtf8(rSelectedFile.native()));
		}

		if (!selectedFiles.empty() && !EndsWithCaseInsensitive(target.native(), L".vcxproj"))
		{
			FailBuild("--files requires a .vcxproj target");
			return EmitResult(kiExitFailure);
		}
		if (!std::filesystem::is_regular_file(target))
		{
			FailBuild("build target does not exist");
			return EmitResult(kiExitFailure);
		}

		std::optional<std::filesystem::path> worktreeRoot = FindWorktreeRoot(target);
		std::optional<std::filesystem::path> msbuildPath = FindMsBuild();
		if (worktreeRoot)
		{
			result["worktreeRoot"] = { { "path", WideToUtf8(worktreeRoot->native()) }, { "normalized", WideToUtf8(ComparablePath(*worktreeRoot)) } };
		}
		if (msbuildPath)
		{
			result["msbuild"]["discovered"] = true;
			result["msbuild"]["path"] = WideToUtf8(msbuildPath->native());
		}
		if (!worktreeRoot || !msbuildPath)
		{
			FailBuild("could not locate worktree root or MSBuild");
			return EmitResult(kiExitFailure);
		}

		RetainedLog retainedLog;
		if (!retainedLog.Open(*worktreeRoot / L"Temp" / L"AgentBuildLogs", target.stem().native()))
		{
			return EmitResult(kiExitFailure);
		}
		result["retainedLog"] = { { "path", WideToUtf8(retainedLog.GetPath().native()) }, { "bytes", 0 }, { "complete", false } };

		std::filesystem::path lockPath;
		int64_t iWaitedSeconds = 0;
		const char* pLockDisposition = "failed";
		std::optional<Handle> buildLock = AcquireBuildLock(*worktreeRoot, target, lockPath, iWaitedSeconds, pLockDisposition);
		result["lock"]["path"] = WideToUtf8(lockPath.native());
		result["lock"]["waitedSeconds"] = iWaitedSeconds;
		result["lock"]["disposition"] = pLockDisposition;
		if (!buildLock)
		{
			return EmitResult(kiExitFailure);
		}

		DiagnosticParser parser;
		const auto FinalizeStreams = [&result, &retainedLog, &parser]()
		{
			result["retainedLog"]["bytes"] = retainedLog.GetBytes();
			result["retainedLog"]["complete"] = !retainedLog.HasFailure();
			result["diagnostics"] = parser.GetDiagnostics();
			result["diagnosticsTruncated"] = parser.WereDiagnosticsTruncated();
		};

		if (!selectedFiles.empty() && !InvalidateSelectedObjects(*msbuildPath, target, buildArguments, selectedFiles, retainedLog, result["invalidatedObjects"]))
		{
			FinalizeStreams();
			return EmitResult(kiExitFailure);
		}

		std::vector<std::wstring> arguments = { msbuildPath->native(), target.native() };
		arguments.insert(arguments.end(), buildArguments.begin(), buildArguments.end());
		// Persistent MSBuild worker nodes inherit the pipe write handle and would stall the
		// drain long after the build completes; honor an explicit caller choice when present.
		bool bHasNodeReuse = false;
		for (const std::wstring& rArgument : buildArguments)
		{
			std::wstring lower = ToLowerInvariant(rArgument);
			if (lower.starts_with(L"/nodereuse:") || lower.starts_with(L"-nodereuse:") || lower.starts_with(L"/nr:") || lower.starts_with(L"-nr:"))
			{
				bHasNodeReuse = true;
				break;
			}
		}
		if (!bHasNodeReuse)
		{
			arguments.emplace_back(L"/nodeReuse:false");
		}
		std::wcerr << L"WorktreeCli: building " << target.native() << L'\n' << std::flush;
		std::optional<DWORD> msbuildExitCode = RunMsBuildToLog(*msbuildPath, arguments, retainedLog, parser);
		FinalizeStreams();
		if (!msbuildExitCode)
		{
			return EmitResult(kiExitFailure);
		}

		result["msbuild"]["launched"] = true;
		result["msbuild"]["exitCode"] = *msbuildExitCode;
		if (*msbuildExitCode != 0)
		{
			result["failureKind"] = "msbuild";
			return EmitResult(static_cast<int>(*msbuildExitCode));
		}
		if (retainedLog.HasFailure())
		{
			// A successful build without its complete retained log is a visible tool failure.
			return EmitResult(kiExitFailure);
		}
		result["status"] = "success";
		result["failureKind"] = "none";
		return EmitResult(kiExitOk);
	}

	// The stdout contract is exactly one schema-versioned result even when the body throws
	// (filesystem errors, regex limits, allocation failure); emit a minimal valid object then.
	static int EmitFallbackBuildResult(std::string_view message)
	{
		Fail(message);
		nlohmann::json result;
		result["schemaVersion"] = kBuildResultSchema;
		result["status"] = "fail";
		result["failureKind"] = "tool";
		result["startedAt"] = coordination::CurrentUtcTimestamp();
		result["target"] = nullptr;
		result["worktreeRoot"] = nullptr;
		result["arguments"] = nlohmann::json::array();
		result["selectedFiles"] = nlohmann::json::array();
		result["invalidatedObjects"] = nlohmann::json::array();
		result["lock"] = { { "disposition", "not-attempted" }, { "path", nullptr }, { "waitedSeconds", 0 } };
		result["msbuild"] = { { "discovered", false }, { "path", nullptr }, { "launched", false }, { "exitCode", nullptr } };
		result["exitCode"] = kiExitFailure;
		result["elapsedMilliseconds"] = 0;
		result["retainedLog"] = nullptr;
		result["diagnostics"] = nlohmann::json::array();
		result["diagnosticsTruncated"] = false;
		result["messages"] = sBuildMessages;
		result["messages"].push_back(std::string(message));
		std::cout << result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
		return kiExitFailure;
	}

	int RunBuildCommand(int iArgumentCount, wchar_t* pArgumentValues[])
	{
		try
		{
			return RunBuildCommandUnguarded(iArgumentCount, pArgumentValues);
		}
		catch (const std::exception& rException)
		{
			return EmitFallbackBuildResult(std::string("unhandled build failure: ") + rException.what());
		}
		catch (...)
		{
			return EmitFallbackBuildResult("unhandled non-standard build failure");
		}
	}
}
