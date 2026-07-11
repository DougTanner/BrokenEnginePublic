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
		constexpr DWORD kuiGuardWaitMilliseconds = 10000;

		struct LockLocator
		{
			std::wstring domain;
			std::wstring logicalKey;
			std::filesystem::path path;
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

		std::optional<LockLocator> ParseLocator(int iArgumentCount, wchar_t* pArgumentValues[], int iStartIndex, std::wstring& rOwner, std::wstring& rExpectedOwner, std::wstring& rSession, std::wstring& rWorktree)
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
			Fail("lock requires token, claim, status, release, or steal");
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
		if (verb != L"claim" && verb != L"status" && verb != L"release" && verb != L"steal")
		{
			Fail("unknown lock verb");
			return kiExitFailure;
		}

		std::wstring owner;
		std::wstring expectedOwner;
		std::wstring session;
		std::wstring worktree;
		std::optional<LockLocator> locator = ParseLocator(iArgumentCount, pArgumentValues, 3, owner, expectedOwner, session, worktree);
		if (!locator)
		{
			return kiExitFailure;
		}
		if ((verb == L"claim" || verb == L"steal") && (owner.empty() || session.empty() || worktree.empty()))
		{
			Fail("claim and steal require --owner, --session, and --worktree");
			return kiExitFailure;
		}
		if (verb == L"release" && owner.empty())
		{
			Fail("release requires --owner");
			return kiExitFailure;
		}
		if (verb == L"steal" && expectedOwner.empty())
		{
			Fail("steal requires --expect");
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
		if (bExists && !ReadMetadata(locator->path, metadata))
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
			PrintMetadata(metadata);
			return kiExitOk;
		}

		if (verb == L"claim")
		{
			if (bExists)
			{
				PrintMetadata(metadata);
				return kiExitStateConflict;
			}
			metadata = NewMetadata(*locator, owner, session, worktree);
			if (!WriteMetadataAtomic(locator->path, metadata))
			{
				FailWindows("write lock metadata");
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

		metadata = NewMetadata(*locator, owner, session, worktree);
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
