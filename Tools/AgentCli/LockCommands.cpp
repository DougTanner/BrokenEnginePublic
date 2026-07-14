#include "LockCommands.h"

#include "AgentCliCommon.h"
#include "CoordinationStore.h"
#include "LandingLockLifecycle.h"

#include <bcrypt.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>

#include "tinygltf/json.hpp"

#pragma comment(lib, "bcrypt.lib")

namespace agentcli
{
	namespace
	{
		using coordination::CurrentUtcTicks;
		using coordination::CurrentUtcTimestamp;
		using coordination::FormatUtcTimestamp;
		using coordination::Guard;
		using coordination::HasOwner;
		using coordination::JsonIntegerEquals;
		using coordination::Locator;
		using coordination::NewMetadata;
		using coordination::ParseUtcTimestamp;
		using coordination::PrintMetadata;
		using coordination::ReadMetadata;
		using coordination::WriteMetadataAtomic;

		std::optional<Locator> MakeLocator(const std::wstring& rDomain, const std::wstring& rKey, const std::wstring& rRepository)
		{
			if (rDomain != L"harness" && rDomain != L"landing")
			{
				Fail("--domain must be harness or landing");
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
				logicalKey = coordination::CanonicalizeDirectoryPath(rRepository);
			}
			else
			{
				if (rKey.empty() || !rRepository.empty())
				{
					Fail("harness locator requires --key and does not accept --repo");
					return std::nullopt;
				}
				logicalKey = coordination::NormalizeRelativeKey(rKey);
			}
			if (!logicalKey)
			{
				Fail("invalid lock logical key");
				return std::nullopt;
			}

			std::optional<Locator> locator = coordination::MakeLocator(rDomain, *logicalKey);
			if (!locator)
			{
				Fail("could not resolve lock storage");
				return std::nullopt;
			}
			return locator;
		}

		std::optional<Locator> ParseLocator(int iArgumentCount, wchar_t* pArgumentValues[], int iStartIndex, std::wstring& rOwner, std::wstring& rExpectedOwner, std::wstring& rSession, std::wstring& rWorktree, int64_t& riLeaseSeconds)
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
		std::optional<Locator> locator = ParseLocator(iArgumentCount, pArgumentValues, 3, owner, expectedOwner, session, worktree, iLeaseSeconds);
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
		if (locator->domain == L"landing" && ((verb == L"claim" || verb == L"recover") && !landing::IsValidLeaseDuration(iLeaseSeconds)))
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
		if (!coordination::EnsureParentDirectory(locator->path))
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
			PrintMetadata(locator->domain == L"landing" ? landing::LandingStatus(metadata, *locator) : metadata);
			return kiExitOk;
		}

		if (verb == L"claim")
		{
			if (bExists)
			{
				PrintMetadata(metadata);
				return kiExitStateConflict;
			}
			metadata = locator->domain == L"landing" ? landing::NewLandingMetadata(*locator, owner, session, worktree, iLeaseSeconds) : NewMetadata(*locator, owner, session, worktree);
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
			std::optional<landing::LandingLease> lease = bExists ? landing::ValidateLandingLease(metadata, *locator, CurrentUtcTicks()) : std::nullopt;
			if (!lease || lease->owner != WideToUtf8(owner))
			{
				if (bExists)
				{
					PrintMetadata(landing::LandingStatus(metadata, *locator));
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
			std::optional<landing::LandingLease> lease = bExists ? landing::ValidateLandingLease(metadata, *locator, uiNow) : std::nullopt;
			if (!lease || lease->owner != WideToUtf8(expectedOwner) || uiNow < lease->uiExpiresTicks || !landing::AllRegisteredWorktreesClear(*locator))
			{
				if (bExists)
				{
					PrintMetadata(landing::LandingStatus(metadata, *locator));
				}
				return kiExitStateConflict;
			}
			nlohmann::json revalidatedMetadata;
			if (!ReadMetadata(locator->path, revalidatedMetadata) || revalidatedMetadata != metadata)
			{
				return kiExitStateConflict;
			}
			metadata = landing::NewLandingMetadata(*locator, owner, session, worktree, iLeaseSeconds);
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

		if (locator->domain == L"landing" && metadata.contains("schemaVersion") && JsonIntegerEquals(metadata["schemaVersion"], landing::kiLandingLeaseSchemaVersion))
		{
			PrintMetadata(landing::LandingStatus(metadata, *locator));
			return kiExitStateConflict;
		}
		if (locator->domain == L"landing" && (!metadata.contains("schemaVersion") || (!JsonIntegerEquals(metadata["schemaVersion"], 1) && !JsonIntegerEquals(metadata["schemaVersion"], 2))))
		{
			PrintMetadata(landing::LandingStatus(metadata, *locator));
			return kiExitStateConflict;
		}
		if (locator->domain == L"landing" && !landing::IsValidLeaseDuration(iLeaseSeconds))
		{
			Fail("legacy landing steal requires --lease-seconds in the range 60..86400");
			return kiExitFailure;
		}

		metadata = locator->domain == L"landing" ? landing::NewLandingMetadata(*locator, owner, session, worktree, iLeaseSeconds) : NewMetadata(*locator, owner, session, worktree);
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
		std::optional<Locator> locator = MakeLocator(L"harness", L"default", L"");
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
