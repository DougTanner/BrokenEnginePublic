#include "LandingLockCommands.h"

#include "ToolCliCommon.h"
#include "CoordinationStore.h"
#include "LandingLockLifecycle.h"

#include <filesystem>
#include <iostream>
#include <optional>

namespace toolcli
{
	namespace
	{
		using coordination::CurrentUtcTicks;
		using coordination::CurrentUtcTimestamp;
		using coordination::FormatUtcTimestamp;
		using coordination::Guard;
		using coordination::HasOwner;
		using coordination::Locator;
		using coordination::ParseUtcTimestamp;
		using coordination::PrintMetadata;
		using coordination::ReadMetadata;
		using coordination::WriteMetadataAtomic;

		std::optional<Locator> MakeLandingLocator(const std::wstring& rRepository)
		{
			if (rRepository.empty())
			{
				Fail("landing lock requires --repo");
				return std::nullopt;
			}

			std::optional<std::wstring> logicalKey = coordination::CanonicalizeDirectoryPath(rRepository);
			if (!logicalKey)
			{
				Fail("invalid lock logical key");
				return std::nullopt;
			}

			std::optional<Locator> locator = coordination::MakeLocator(L"landing", *logicalKey);
			if (!locator)
			{
				Fail("could not resolve lock storage");
				return std::nullopt;
			}
			return locator;
		}

		std::optional<Locator> ParseLocator(int iArgumentCount, wchar_t* pArgumentValues[], int iStartIndex, std::wstring& rOwner, std::wstring& rExpectedOwner, std::wstring& rSession, std::wstring& rWorktree, int64_t& riLeaseSeconds)
		{
			std::wstring repository;
			for (int i = iStartIndex; i < iArgumentCount; ++i)
			{
				std::wstring_view argument = pArgumentValues[i];
				std::wstring* pDestination = nullptr;
				if (argument == L"--repo")
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
			return MakeLandingLocator(repository);
		}
	}

	int RunLandingLockCommand(int iArgumentCount, wchar_t* pArgumentValues[])
	{
		if (iArgumentCount < 3)
		{
			Fail("lock requires claim, status, refresh, recover, release, or steal");
			return kiExitFailure;
		}
		std::wstring verb = ToLowerInvariant(pArgumentValues[2]);
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
		if ((verb == L"claim" || verb == L"recover") && !landing::IsValidLeaseDuration(iLeaseSeconds))
		{
			Fail("landing claim and recover require --lease-seconds in the range 60..86400");
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
			Fail("could not acquire lock transition guard (" + guard.FailureReason() + ")");
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
		if (bExists && !bReadable && verb != L"status")
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
			PrintMetadata(landing::LandingStatus(metadata, *locator));
			return kiExitOk;
		}

		if (verb == L"claim")
		{
			if (bExists)
			{
				PrintMetadata(landing::LandingStatus(metadata, *locator));
				return kiExitStateConflict;
			}
			metadata = landing::NewLandingMetadata(*locator, owner, session, worktree, iLeaseSeconds);
			if (!WriteMetadataAtomic(locator->path, metadata))
			{
				FailWindows("write lock metadata");
				return kiExitFailure;
			}
			PrintMetadata(landing::LandingStatus(metadata, *locator));
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
			PrintMetadata(landing::LandingStatus(metadata, *locator));
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
			PrintMetadata(landing::LandingStatus(metadata, *locator));
			return kiExitOk;
		}

		if (!bExists || (verb == L"release" && !HasOwner(metadata, owner)) || (verb == L"steal" && !HasOwner(metadata, expectedOwner)))
		{
			if (bExists)
			{
				PrintMetadata(landing::LandingStatus(metadata, *locator));
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

		// steal: a lease-based landing lock is never stolen; recover is the expired-lease takeover.
		PrintMetadata(landing::LandingStatus(metadata, *locator));
		return kiExitStateConflict;
	}

}
