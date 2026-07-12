#include "LockCommands.h"

#include "AgentCliCommon.h"

#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>
#include <vector>

#include "tinygltf/json.hpp"

#pragma comment(lib, "bcrypt.lib")

namespace agentcli
{
	namespace
	{
		constexpr int kiSchemaVersion = 2;
		constexpr int kiLandingLeaseSchemaVersion = 3;
		constexpr int64_t kiMinimumLeaseSeconds = 60;
		constexpr int64_t kiMaximumLeaseSeconds = 86'400;
		constexpr DWORD kuiGuardWaitMilliseconds = 10000;

		struct LockLocator
		{
			std::wstring domain;
			std::wstring logicalKey;
			std::filesystem::path path;
		};

		struct LandingLease
		{
			std::string owner;
			std::string claimedAt;
			std::string heartbeatAt;
			std::string expiresAt;
			int64_t iDurationSeconds = 0;
			uint64_t uiClaimedTicks = 0;
			uint64_t uiHeartbeatTicks = 0;
			uint64_t uiExpiresTicks = 0;
		};

		class Guard
		{
		public:
			explicit Guard(const std::filesystem::path& rPath)
			{
				const std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(kuiGuardWaitMilliseconds);
				do
				{
					mhFile.Reset(::CreateFileW(rPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr));
					if (mhFile.IsValid())
					{
						return;
					}
					if (::GetLastError() != ERROR_SHARING_VIOLATION && ::GetLastError() != ERROR_LOCK_VIOLATION)
					{
						return;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(25));
				}
				while (std::chrono::steady_clock::now() < endTime);
			}

			[[nodiscard]] bool IsValid() const
			{
				return mhFile.IsValid();
			}

		private:
			Handle mhFile;
		};

		std::string CurrentUtcTimestamp()
		{
			SYSTEMTIME time {};
			::GetSystemTime(&time);
			char pBuffer[32] {};
			std::snprintf(pBuffer, sizeof(pBuffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
			return pBuffer;
		}

		bool ParseUtcTimestamp(const std::string& rValue, uint64_t& rTicks)
		{
			SYSTEMTIME time {};
			char cSuffix = 0;
			int iRead = std::sscanf(rValue.c_str(), "%hu-%hu-%huT%hu:%hu:%hu.%hu%c", &time.wYear, &time.wMonth, &time.wDay, &time.wHour, &time.wMinute, &time.wSecond, &time.wMilliseconds, &cSuffix);
			FILETIME fileTime {};
			if (iRead != 8 || cSuffix != 'Z' || rValue.size() != 24 || ::SystemTimeToFileTime(&time, &fileTime) == FALSE)
			{
				return false;
			}
			rTicks = (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
			return true;
		}

		uint64_t CurrentUtcTicks()
		{
			FILETIME fileTime {};
			::GetSystemTimeAsFileTime(&fileTime);
			return (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
		}

		std::string FormatUtcTimestamp(uint64_t uiTicks)
		{
			FILETIME fileTime { static_cast<DWORD>(uiTicks), static_cast<DWORD>(uiTicks >> 32) };
			SYSTEMTIME time {};
			::FileTimeToSystemTime(&fileTime, &time);
			char pBuffer[32] {};
			std::snprintf(pBuffer, sizeof(pBuffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
			return pBuffer;
		}

		std::optional<std::string> RunGit(const std::vector<std::wstring>& rArguments)
		{
			SECURITY_ATTRIBUTES securityAttributes { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
			Handle hRead;
			Handle hWrite;
			HANDLE hRawRead = INVALID_HANDLE_VALUE;
			HANDLE hRawWrite = INVALID_HANDLE_VALUE;
			if (::CreatePipe(&hRawRead, &hRawWrite, &securityAttributes, 0) == FALSE)
			{
				return std::nullopt;
			}
			hRead.Reset(hRawRead);
			hWrite.Reset(hRawWrite);
			::SetHandleInformation(hRead.Get(), HANDLE_FLAG_INHERIT, 0);

			std::vector<std::wstring> arguments { L"git.exe" };
			arguments.insert(arguments.end(), rArguments.begin(), rArguments.end());
			std::wstring commandLine = BuildCommandLine(arguments);
			STARTUPINFOW startupInfo {};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESTDHANDLES;
			startupInfo.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
			startupInfo.hStdOutput = hWrite.Get();
			startupInfo.hStdError = hWrite.Get();
			PROCESS_INFORMATION processInformation {};
			if (::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInformation) == FALSE)
			{
				return std::nullopt;
			}
			Handle hProcess(processInformation.hProcess);
			Handle hThread(processInformation.hThread);
			hWrite.Reset();
			std::string output;
			char pBuffer[4096] {};
			DWORD uiRead = 0;
			while (::ReadFile(hRead.Get(), pBuffer, sizeof(pBuffer), &uiRead, nullptr) != FALSE && uiRead != 0)
			{
				output.append(pBuffer, uiRead);
			}
			::WaitForSingleObject(hProcess.Get(), INFINITE);
			DWORD uiExitCode = 1;
			::GetExitCodeProcess(hProcess.Get(), &uiExitCode);
			return uiExitCode == 0 ? std::optional<std::string>(std::move(output)) : std::nullopt;
		}

		std::optional<std::string> HashSha256(std::string_view value)
		{
			BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
			if (::BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
			{
				return std::nullopt;
			}

			DWORD uiObjectLength = 0;
			DWORD uiResultLength = 0;
			if (::BCryptGetProperty(hAlgorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&uiObjectLength), sizeof(uiObjectLength), &uiResultLength, 0) < 0)
			{
				::BCryptCloseAlgorithmProvider(hAlgorithm, 0);
				return std::nullopt;
			}

			std::vector<UCHAR> hashObject(uiObjectLength);
			BCRYPT_HASH_HANDLE hHash = nullptr;
			if (::BCryptCreateHash(hAlgorithm, &hHash, hashObject.data(), uiObjectLength, nullptr, 0, 0) < 0 ||
				::BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())), static_cast<ULONG>(value.size()), 0) < 0)
			{
				if (hHash != nullptr)
				{
					::BCryptDestroyHash(hHash);
				}
				::BCryptCloseAlgorithmProvider(hAlgorithm, 0);
				return std::nullopt;
			}

			UCHAR pDigest[32] {};
			bool bSucceeded = ::BCryptFinishHash(hHash, pDigest, static_cast<ULONG>(sizeof(pDigest)), 0) >= 0;
			::BCryptDestroyHash(hHash);
			::BCryptCloseAlgorithmProvider(hAlgorithm, 0);
			if (!bSucceeded)
			{
				return std::nullopt;
			}

			constexpr char pHex[] = "0123456789abcdef";
			std::string result;
			result.reserve(sizeof(pDigest) * 2);
			for (UCHAR uiByte : pDigest)
			{
				result.push_back(pHex[uiByte >> 4]);
				result.push_back(pHex[uiByte & 0x0f]);
			}
			return result;
		}

		std::optional<std::wstring> CanonicalizeRepositoryPath(const std::wstring& rValue)
		{
			std::error_code error;
			std::filesystem::path path = std::filesystem::canonical(rValue, error);
			if (error || !std::filesystem::is_directory(path, error))
			{
				return std::nullopt;
			}

			Handle hDirectory(::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
			if (!hDirectory.IsValid())
			{
				return std::nullopt;
			}

			std::wstring finalPath(32768, L'\0');
			DWORD uiWritten = ::GetFinalPathNameByHandleW(hDirectory.Get(), finalPath.data(), static_cast<DWORD>(finalPath.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (uiWritten == 0 || uiWritten >= finalPath.size())
			{
				return std::nullopt;
			}
			finalPath.resize(uiWritten);
			if (finalPath.starts_with(L"\\\\?\\"))
			{
				finalPath.erase(0, 4);
			}
			std::replace(finalPath.begin(), finalPath.end(), L'/', L'\\');
			while (finalPath.size() > 3 && finalPath.back() == L'\\')
			{
				finalPath.pop_back();
			}
			finalPath = ToLowerInvariant(std::move(finalPath));
			if (finalPath.empty())
			{
				return std::nullopt;
			}
			return finalPath;
		}

		std::optional<std::wstring> NormalizeLogicalKey(const std::wstring& rValue)
		{
			if (rValue.empty() || std::filesystem::path(rValue).is_absolute())
			{
				return std::nullopt;
			}

			std::filesystem::path normalized = std::filesystem::path(rValue).lexically_normal();
			if (normalized.empty() || normalized == L".")
			{
				return std::nullopt;
			}
			for (const std::filesystem::path& rPart : normalized)
			{
				if (rPart == L"..")
				{
					return std::nullopt;
				}
			}

			std::wstring result = normalized.native();
			std::replace(result.begin(), result.end(), L'/', L'\\');
			result = ToLowerInvariant(std::move(result));
			if (result.empty())
			{
				return std::nullopt;
			}
			return result;
		}

		std::optional<LockLocator> MakeLocator(const std::wstring& rDomain, const std::wstring& rKey, const std::wstring& rRepository)
		{
			if (rDomain != L"harness" && rDomain != L"plan" && rDomain != L"landing")
			{
				Fail("--domain must be harness, plan, or landing");
				return std::nullopt;
			}

			std::optional<std::wstring> logicalKey;
			if (rDomain == L"landing")
			{
				if (rRepository.empty() || !rKey.empty())
				{
					Fail("landing locator requires --repo and does not accept --key");
					return std::nullopt;
				}
				logicalKey = CanonicalizeRepositoryPath(rRepository);
			}
			else
			{
				if (rKey.empty() || !rRepository.empty())
				{
					Fail("harness and plan locators require --key and do not accept --repo");
					return std::nullopt;
				}
				logicalKey = NormalizeLogicalKey(rKey);
			}
			if (!logicalKey)
			{
				Fail("invalid lock logical key");
				return std::nullopt;
			}

			std::string logicalKeyUtf8 = WideToUtf8(*logicalKey);
			std::optional<std::string> hash = HashSha256(logicalKeyUtf8);
			std::filesystem::path localApplicationData = GetLocalApplicationDataPath();
			if (!hash || localApplicationData.empty())
			{
				Fail("could not resolve lock storage");
				return std::nullopt;
			}

			LockLocator locator;
			locator.domain = rDomain;
			locator.logicalKey = *logicalKey;
			locator.path = localApplicationData / L"BrokenEngineLocks" / rDomain / Utf8ToWide(*hash + ".lock");
			return locator;
		}

		bool ReadMetadata(const std::filesystem::path& rPath, nlohmann::json& rMetadata)
		{
			std::ifstream input(rPath, std::ios::binary);
			if (!input)
			{
				return false;
			}
			try
			{
				input >> rMetadata;
				return rMetadata.is_object();
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		bool WriteMetadataAtomic(const std::filesystem::path& rPath, const nlohmann::json& rMetadata)
		{
			static uint32_t suiSequence = 0;
			std::filesystem::path temporaryPath = rPath;
			temporaryPath += L".tmp." + std::to_wstring(::GetCurrentProcessId()) + L"." + std::to_wstring(++suiSequence);
			std::string contents = rMetadata.dump(2) + "\n";

			Handle hFile(::CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY, nullptr));
			if (!hFile.IsValid())
			{
				return false;
			}

			DWORD uiWritten = 0;
			bool bSucceeded = ::WriteFile(hFile.Get(), contents.data(), static_cast<DWORD>(contents.size()), &uiWritten, nullptr) != FALSE && uiWritten == contents.size() && ::FlushFileBuffers(hFile.Get()) != FALSE;
			hFile.Reset();
			if (bSucceeded)
			{
				bSucceeded = ::MoveFileExW(temporaryPath.c_str(), rPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
			}
			if (!bSucceeded)
			{
				::DeleteFileW(temporaryPath.c_str());
			}
			return bSucceeded;
		}

		void PrintMetadata(const nlohmann::json& rMetadata)
		{
			std::cout << rMetadata.dump(2) << '\n';
		}

		std::optional<LockLocator> ParseLocator(int iArgumentCount, wchar_t* pArgumentValues[], int iStartIndex, std::wstring& rOwner, std::wstring& rExpectedOwner, std::wstring& rSession, std::wstring& rWorktree, int64_t& riLeaseSeconds)
		{
			std::wstring domain;
			std::wstring key;
			std::wstring repository;
			for (int i = iStartIndex; i < iArgumentCount; ++i)
			{
				std::wstring_view argument = pArgumentValues[i];
				std::wstring* pDestination = nullptr;
				if (argument == L"--domain")
				{
					pDestination = &domain;
				}
				else if (argument == L"--key")
				{
					pDestination = &key;
				}
				else if (argument == L"--repo")
				{
					pDestination = &repository;
				}
				else if (argument == L"--owner")
				{
					pDestination = &rOwner;
				}
				else if (argument == L"--expect")
				{
					pDestination = &rExpectedOwner;
				}
				else if (argument == L"--session")
				{
					pDestination = &rSession;
				}
				else if (argument == L"--worktree")
				{
					pDestination = &rWorktree;
				}
				else if (argument == L"--lease-seconds")
				{
					if (++i >= iArgumentCount)
					{
						Fail("lock option requires a value");
						return std::nullopt;
					}
					wchar_t* pEnd = nullptr;
					riLeaseSeconds = std::wcstoll(pArgumentValues[i], &pEnd, 10);
					if (pEnd == pArgumentValues[i] || *pEnd != L'\0')
					{
						Fail("--lease-seconds must be an integer");
						return std::nullopt;
					}
					continue;
				}
				else
				{
					Fail("unknown lock argument: " + WideToUtf8(argument));
					return std::nullopt;
				}
				if (++i >= iArgumentCount)
				{
					Fail("lock option requires a value");
					return std::nullopt;
				}
				*pDestination = pArgumentValues[i];
			}
			return MakeLocator(ToLowerInvariant(std::move(domain)), key, repository);
		}

		bool IsValidLeaseDuration(int64_t iLeaseSeconds)
		{
			return iLeaseSeconds >= kiMinimumLeaseSeconds && iLeaseSeconds <= kiMaximumLeaseSeconds;
		}

		bool JsonIntegerEquals(const nlohmann::json& rValue, int64_t iExpected)
		{
			if (rValue.is_number_unsigned())
			{
				return iExpected >= 0 && rValue.get<uint64_t>() == static_cast<uint64_t>(iExpected);
			}
			return rValue.is_number_integer() && rValue.get<int64_t>() == iExpected;
		}

		std::optional<int64_t> JsonInt64(const nlohmann::json& rValue)
		{
			if (rValue.is_number_unsigned())
			{
				const uint64_t uiValue = rValue.get<uint64_t>();
				return uiValue <= static_cast<uint64_t>(INT64_MAX) ? std::optional<int64_t>(static_cast<int64_t>(uiValue)) : std::nullopt;
			}
			return rValue.is_number_integer() ? std::optional<int64_t>(rValue.get<int64_t>()) : std::nullopt;
		}

		nlohmann::json NewMetadata(const LockLocator& rLocator, const std::wstring& rOwner, const std::wstring& rSession, const std::wstring& rWorktree)
		{
			const std::string timestamp = CurrentUtcTimestamp();
			return {
				{ "schemaVersion", kiSchemaVersion },
				{ "domain", WideToUtf8(rLocator.domain) },
				{ "logicalKey", WideToUtf8(rLocator.logicalKey) },
				{ "owner", WideToUtf8(rOwner) },
				{ "session", WideToUtf8(rSession) },
				{ "worktree", WideToUtf8(rWorktree) },
				{ "claimantPid", ::GetCurrentProcessId() },
				{ "claimedAt", timestamp },
				{ "heartbeatAt", timestamp },
			};
		}

		nlohmann::json NewLandingMetadata(const LockLocator& rLocator, const std::wstring& rOwner, const std::wstring& rSession, const std::wstring& rWorktree, int64_t iLeaseSeconds)
		{
			nlohmann::json metadata = NewMetadata(rLocator, rOwner, rSession, rWorktree);
			metadata["schemaVersion"] = kiLandingLeaseSchemaVersion;
			metadata["leaseDurationSeconds"] = iLeaseSeconds;
			uint64_t uiHeartbeatTicks = 0;
			ParseUtcTimestamp(metadata["heartbeatAt"].get<std::string>(), uiHeartbeatTicks);
			uint64_t uiExpiresTicks = uiHeartbeatTicks + static_cast<uint64_t>(iLeaseSeconds) * 10'000'000ull;
			metadata["expiresAt"] = FormatUtcTimestamp(uiExpiresTicks);
			return metadata;
		}

		std::optional<LandingLease> ValidateLandingLease(const nlohmann::json& rMetadata, const LockLocator& rLocator, uint64_t uiCurrentTicks)
		{
			const char* pStringFields[] = { "owner", "session", "worktree", "claimedAt", "heartbeatAt", "expiresAt" };
			if (!rMetadata.contains("schemaVersion") || !JsonIntegerEquals(rMetadata["schemaVersion"], kiLandingLeaseSchemaVersion) ||
				!rMetadata.contains("domain") || !rMetadata["domain"].is_string() || rMetadata["domain"].get<std::string>() != "landing" ||
				!rMetadata.contains("logicalKey") || !rMetadata["logicalKey"].is_string() || rMetadata["logicalKey"].get<std::string>() != WideToUtf8(rLocator.logicalKey) ||
				!rMetadata.contains("leaseDurationSeconds"))
			{
				return std::nullopt;
			}
			const std::optional<int64_t> durationSeconds = JsonInt64(rMetadata["leaseDurationSeconds"]);
			if (!durationSeconds)
			{
				return std::nullopt;
			}
			for (const char* pField : pStringFields)
			{
				if (!rMetadata.contains(pField) || !rMetadata[pField].is_string() || rMetadata[pField].get<std::string>().empty())
				{
					return std::nullopt;
				}
			}
			LandingLease lease;
			lease.owner = rMetadata["owner"].get<std::string>();
			lease.claimedAt = rMetadata["claimedAt"].get<std::string>();
			lease.heartbeatAt = rMetadata["heartbeatAt"].get<std::string>();
			lease.expiresAt = rMetadata["expiresAt"].get<std::string>();
			lease.iDurationSeconds = *durationSeconds;
			if (!IsValidLeaseDuration(lease.iDurationSeconds) || !ParseUtcTimestamp(lease.claimedAt, lease.uiClaimedTicks) || !ParseUtcTimestamp(lease.heartbeatAt, lease.uiHeartbeatTicks) || !ParseUtcTimestamp(lease.expiresAt, lease.uiExpiresTicks) ||
				lease.uiHeartbeatTicks > UINT64_MAX - static_cast<uint64_t>(lease.iDurationSeconds) * 10'000'000ull || lease.uiExpiresTicks != lease.uiHeartbeatTicks + static_cast<uint64_t>(lease.iDurationSeconds) * 10'000'000ull ||
				lease.uiClaimedTicks > lease.uiHeartbeatTicks || lease.uiHeartbeatTicks > uiCurrentTicks)
			{
				return std::nullopt;
			}
			return lease;
		}

		nlohmann::json LandingStatus(const nlohmann::json& rMetadata, const LockLocator& rLocator)
		{
			nlohmann::json status = { { "held", true }, { "leaseState", "unverifiable" } };
			for (const char* pField : { "owner", "session", "worktree", "claimedAt", "heartbeatAt", "expiresAt" })
			{
				if (rMetadata.contains(pField) && rMetadata[pField].is_string())
				{
					status[pField] = rMetadata[pField];
				}
			}
			std::optional<LandingLease> lease = ValidateLandingLease(rMetadata, rLocator, CurrentUtcTicks());
			if (lease)
			{
				status = rMetadata;
				status["held"] = true;
				status["leaseState"] = CurrentUtcTicks() < lease->uiExpiresTicks ? "live" : "expired";
			}
			return status;
		}

		bool AllRegisteredWorktreesClear(const LockLocator& rLocator)
		{
			std::optional<std::string> listing = RunGit({ L"--git-dir", rLocator.logicalKey, L"worktree", L"list", L"--porcelain", L"-z" });
			if (!listing)
			{
				return false;
			}
			std::vector<std::wstring> worktrees;
			std::wstring currentWorktree;
			bool bInvalidEntry = false;
			for (size_t uiStart = 0; uiStart < listing->size();)
			{
				size_t uiEnd = listing->find('\0', uiStart);
				if (uiEnd == std::string::npos)
				{
					uiEnd = listing->size();
				}
				std::string_view field(listing->data() + uiStart, uiEnd - uiStart);
				if (field.empty())
				{
					if (currentWorktree.empty() || bInvalidEntry)
					{
						return false;
					}
					worktrees.push_back(currentWorktree);
					currentWorktree.clear();
					bInvalidEntry = false;
				}
				else if (field.starts_with("worktree "))
				{
					currentWorktree = Utf8ToWide(field.substr(9));
				}
				else if (field == "bare" || field.starts_with("prunable"))
				{
					bInvalidEntry = true;
				}
				uiStart = uiEnd + 1;
			}
			if (!currentWorktree.empty())
			{
				if (bInvalidEntry)
				{
					return false;
				}
				worktrees.push_back(currentWorktree);
			}
			if (worktrees.empty())
			{
				return false;
			}
			const std::filesystem::path pMarkers[] = { L"MERGE_HEAD", L"rebase-merge", L"rebase-apply", L"CHERRY_PICK_HEAD", L"REVERT_HEAD", L"BISECT_LOG", L"sequencer" };
			for (const std::wstring& rWorktree : worktrees)
			{
				std::error_code error;
				if (!std::filesystem::is_directory(rWorktree, error) || error)
				{
					return false;
				}
				std::optional<std::string> gitDirectoryText = RunGit({ L"-C", rWorktree, L"rev-parse", L"--path-format=absolute", L"--git-dir" });
				if (!gitDirectoryText)
				{
					return false;
				}
				while (!gitDirectoryText->empty() && (gitDirectoryText->back() == '\r' || gitDirectoryText->back() == '\n'))
				{
					gitDirectoryText->pop_back();
				}
				std::filesystem::path gitDirectory = Utf8ToWide(*gitDirectoryText);
				if (!std::filesystem::is_directory(gitDirectory, error) || error)
				{
					return false;
				}
				for (const std::filesystem::path& rMarker : pMarkers)
				{
					if (std::filesystem::exists(gitDirectory / rMarker, error) || error)
					{
						return false;
					}
				}
			}
			return true;
		}

		bool HasOwner(const nlohmann::json& rMetadata, const std::wstring& rOwner)
		{
			return rMetadata.contains("owner") && rMetadata["owner"].is_string() && rMetadata["owner"].get<std::string>() == WideToUtf8(rOwner);
		}

		int RunToken()
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
	}

	int RunLockCommand(int iArgumentCount, wchar_t* pArgumentValues[])
	{
		if (iArgumentCount < 3)
		{
			Fail("lock requires token, claim, status, refresh, recover, release, or steal");
			return kiExitFailure;
		}
		std::wstring verb = ToLowerInvariant(pArgumentValues[2]);
		if (verb == L"token")
		{
			if (iArgumentCount != 3)
			{
				Fail("lock token accepts no arguments");
				return kiExitFailure;
			}
			return RunToken();
		}
		if (verb != L"claim" && verb != L"status" && verb != L"refresh" && verb != L"recover" && verb != L"release" && verb != L"steal")
		{
			Fail("unknown lock verb");
			return kiExitFailure;
		}

		std::wstring owner;
		std::wstring expectedOwner;
		std::wstring session;
		std::wstring worktree;
		int64_t iLeaseSeconds = 0;
		std::optional<LockLocator> locator = ParseLocator(iArgumentCount, pArgumentValues, 3, owner, expectedOwner, session, worktree, iLeaseSeconds);
		if (!locator)
		{
			return kiExitFailure;
		}
		if ((verb == L"claim" || verb == L"steal" || verb == L"recover") && (owner.empty() || session.empty() || worktree.empty()))
		{
			Fail("claim, steal, and recover require --owner, --session, and --worktree");
			return kiExitFailure;
		}
		if ((verb == L"release" || verb == L"refresh") && owner.empty())
		{
			Fail("release and refresh require --owner");
			return kiExitFailure;
		}
		if ((verb == L"steal" || verb == L"recover") && expectedOwner.empty())
		{
			Fail("steal and recover require --expect");
			return kiExitFailure;
		}
		if (locator->domain == L"landing" && ((verb == L"claim" || verb == L"recover") && !IsValidLeaseDuration(iLeaseSeconds)))
		{
			Fail("landing claim and recover require --lease-seconds in the range 60..86400");
			return kiExitFailure;
		}
		if (locator->domain != L"landing" && (verb == L"refresh" || verb == L"recover" || iLeaseSeconds != 0))
		{
			Fail("refresh, recover, and --lease-seconds are landing-only");
			return kiExitFailure;
		}

		std::error_code error;
		std::filesystem::create_directories(locator->path.parent_path(), error);
		if (error)
		{
			Fail("could not create lock directory");
			return kiExitFailure;
		}
		Guard guard(locator->path.wstring() + L".guard");
		if (!guard.IsValid())
		{
			Fail("timed out acquiring lock transition guard");
			return kiExitFailure;
		}

		nlohmann::json metadata;
		bool bExists = std::filesystem::exists(locator->path, error);
		if (error)
		{
			Fail("could not inspect lock");
			return kiExitFailure;
		}
		bool bReadable = !bExists || ReadMetadata(locator->path, metadata);
		if (bExists && !bReadable && !(verb == L"status" && locator->domain == L"landing"))
		{
			Fail("lock metadata is unreadable");
			return kiExitFailure;
		}

		if (verb == L"status")
		{
			if (!bExists)
			{
				std::cout << "{\"held\":false}\n";
				return kiExitStateConflict;
			}
			PrintMetadata(locator->domain == L"landing" ? LandingStatus(metadata, *locator) : metadata);
			return kiExitOk;
		}

		if (verb == L"claim")
		{
			if (bExists)
			{
				PrintMetadata(metadata);
				return kiExitStateConflict;
			}
			metadata = locator->domain == L"landing" ? NewLandingMetadata(*locator, owner, session, worktree, iLeaseSeconds) : NewMetadata(*locator, owner, session, worktree);
			if (!WriteMetadataAtomic(locator->path, metadata))
			{
				FailWindows("write lock metadata");
				return kiExitFailure;
			}
			PrintMetadata(metadata);
			return kiExitOk;
		}

		if (verb == L"refresh")
		{
			std::optional<LandingLease> lease = bExists ? ValidateLandingLease(metadata, *locator, CurrentUtcTicks()) : std::nullopt;
			if (!lease || lease->owner != WideToUtf8(owner))
			{
				if (bExists)
				{
					PrintMetadata(LandingStatus(metadata, *locator));
				}
				return kiExitStateConflict;
			}
			const std::string timestamp = CurrentUtcTimestamp();
			uint64_t uiHeartbeatTicks = 0;
			ParseUtcTimestamp(timestamp, uiHeartbeatTicks);
			if (uiHeartbeatTicks < lease->uiHeartbeatTicks)
			{
				return kiExitStateConflict;
			}
			metadata["heartbeatAt"] = timestamp;
			metadata["expiresAt"] = FormatUtcTimestamp(uiHeartbeatTicks + static_cast<uint64_t>(lease->iDurationSeconds) * 10'000'000ull);
			if (!WriteMetadataAtomic(locator->path, metadata))
			{
				FailWindows("refresh lock metadata");
				return kiExitFailure;
			}
			PrintMetadata(metadata);
			return kiExitOk;
		}

		if (verb == L"recover")
		{
			uint64_t uiNow = CurrentUtcTicks();
			std::optional<LandingLease> lease = bExists ? ValidateLandingLease(metadata, *locator, uiNow) : std::nullopt;
			if (!lease || lease->owner != WideToUtf8(expectedOwner) || uiNow < lease->uiExpiresTicks || !AllRegisteredWorktreesClear(*locator))
			{
				if (bExists)
				{
					PrintMetadata(LandingStatus(metadata, *locator));
				}
				return kiExitStateConflict;
			}
			nlohmann::json revalidatedMetadata;
			if (!ReadMetadata(locator->path, revalidatedMetadata) || revalidatedMetadata != metadata)
			{
				return kiExitStateConflict;
			}
			metadata = NewLandingMetadata(*locator, owner, session, worktree, iLeaseSeconds);
			if (!WriteMetadataAtomic(locator->path, metadata))
			{
				FailWindows("recover lock metadata");
				return kiExitFailure;
			}
			PrintMetadata(metadata);
			return kiExitOk;
		}

		if (!bExists || (verb == L"release" && !HasOwner(metadata, owner)) || (verb == L"steal" && !HasOwner(metadata, expectedOwner)))
		{
			if (bExists)
			{
				PrintMetadata(metadata);
			}
			else
			{
				std::cout << "{\"held\":false}\n";
			}
			return kiExitStateConflict;
		}

		if (verb == L"release")
		{
			if (::DeleteFileW(locator->path.c_str()) == FALSE)
			{
				FailWindows("release lock");
				return kiExitFailure;
			}
			return kiExitOk;
		}

		if (locator->domain == L"landing" && metadata.contains("schemaVersion") && JsonIntegerEquals(metadata["schemaVersion"], kiLandingLeaseSchemaVersion))
		{
			PrintMetadata(LandingStatus(metadata, *locator));
			return kiExitStateConflict;
		}
		if (locator->domain == L"landing" && (!metadata.contains("schemaVersion") || (!JsonIntegerEquals(metadata["schemaVersion"], 1) && !JsonIntegerEquals(metadata["schemaVersion"], 2))))
		{
			PrintMetadata(LandingStatus(metadata, *locator));
			return kiExitStateConflict;
		}
		if (locator->domain == L"landing" && !IsValidLeaseDuration(iLeaseSeconds))
		{
			Fail("legacy landing steal requires --lease-seconds in the range 60..86400");
			return kiExitFailure;
		}

		metadata = locator->domain == L"landing" ? NewLandingMetadata(*locator, owner, session, worktree, iLeaseSeconds) : NewMetadata(*locator, owner, session, worktree);
		if (!WriteMetadataAtomic(locator->path, metadata))
		{
			FailWindows("replace lock metadata");
			return kiExitFailure;
		}
		PrintMetadata(metadata);
		return kiExitOk;
	}

	bool RefreshHarnessHeartbeat(const std::wstring& rOwner)
	{
		if (rOwner.empty())
		{
			return true;
		}
		std::optional<LockLocator> locator = MakeLocator(L"harness", L"default", L"");
		if (!locator)
		{
			return false;
		}
		Guard guard(locator->path.wstring() + L".guard");
		if (!guard.IsValid())
		{
			return false;
		}
		nlohmann::json metadata;
		if (!ReadMetadata(locator->path, metadata) || !HasOwner(metadata, rOwner))
		{
			return false;
		}
		metadata["heartbeatAt"] = CurrentUtcTimestamp();
		metadata["heartbeatPid"] = ::GetCurrentProcessId();
		return WriteMetadataAtomic(locator->path, metadata);
	}
}
