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

		enum class LandingRecordState
		{
			kReadable,
			kAbsent,
			kUnverifiable,
		};

		enum class LandingReleaseOperation
		{
			kRelease,
			kSteal,
		};

		int EmitLandingConflict(const Locator& rLocator, const nlohmann::json& rMetadata, LandingRecordState eRecordState)
		{
			if (eRecordState == LandingRecordState::kReadable)
			{
				PrintMetadata(landing::LandingStatus(rMetadata, rLocator));
			}
			else if (eRecordState == LandingRecordState::kAbsent)
			{
				std::cout << "{\"held\":false}\n";
			}
			else
			{
				std::cout << "{\"held\":true,\"leaseState\":\"unverifiable\"}\n";
			}
			return kiExitStateConflict;
		}

		int HandleClaim(const Locator& rLocator, nlohmann::json& rMetadata, bool bExists, const std::wstring& rOwner, const std::wstring& rSession, const std::wstring& rWorktree, int64_t iLeaseSeconds)
		{
			if (bExists)
			{
				return EmitLandingConflict(rLocator, rMetadata, LandingRecordState::kReadable);
			}
			rMetadata = landing::NewLandingMetadata(rLocator, rOwner, rSession, rWorktree, iLeaseSeconds);
			if (!WriteMetadataAtomic(rLocator.path, rMetadata))
			{
				FailWindows("write lock metadata");
				return kiExitFailure;
			}
			PrintMetadata(landing::LandingStatus(rMetadata, rLocator));
			return kiExitOk;
		}

		int HandleRefresh(const Locator& rLocator, nlohmann::json& rMetadata, bool bExists, const std::wstring& rOwner)
		{
			std::optional<landing::LandingLease> lease = bExists ? landing::ValidateLandingLease(rMetadata, rLocator, CurrentUtcTicks()) : std::nullopt;
			if (!lease || lease->owner != WideToUtf8(rOwner))
			{
				return EmitLandingConflict(rLocator, rMetadata, bExists ? LandingRecordState::kReadable : LandingRecordState::kAbsent);
			}
			const std::string timestamp = CurrentUtcTimestamp();
			uint64_t uiHeartbeatTicks = 0;
			ParseUtcTimestamp(timestamp, uiHeartbeatTicks);
			if (uiHeartbeatTicks < lease->uiHeartbeatTicks)
			{
				return EmitLandingConflict(rLocator, rMetadata, LandingRecordState::kReadable);
			}
			rMetadata["heartbeatAt"] = timestamp;
			rMetadata["expiresAt"] = FormatUtcTimestamp(uiHeartbeatTicks + static_cast<uint64_t>(lease->iDurationSeconds) * 10'000'000ull);
			if (!WriteMetadataAtomic(rLocator.path, rMetadata))
			{
				FailWindows("refresh lock metadata");
				return kiExitFailure;
			}
			PrintMetadata(landing::LandingStatus(rMetadata, rLocator));
			return kiExitOk;
		}

		int HandleRecover(const Locator& rLocator, nlohmann::json& rMetadata, bool bExists, const std::wstring& rOwner, const std::wstring& rExpectedOwner, const std::wstring& rSession, const std::wstring& rWorktree, int64_t iLeaseSeconds)
		{
			uint64_t uiNow = CurrentUtcTicks();
			std::optional<landing::LandingLease> lease = bExists ? landing::ValidateLandingLease(rMetadata, rLocator, uiNow) : std::nullopt;
			if (!lease || lease->owner != WideToUtf8(rExpectedOwner) || uiNow < lease->uiExpiresTicks || !landing::AllRegisteredWorktreesClear(rLocator))
			{
				return EmitLandingConflict(rLocator, rMetadata, bExists ? LandingRecordState::kReadable : LandingRecordState::kAbsent);
			}
			nlohmann::json revalidatedMetadata;
			if (!ReadMetadata(rLocator.path, revalidatedMetadata))
			{
				std::error_code error;
				const bool bRevalidatedExists = std::filesystem::exists(rLocator.path, error);
				return EmitLandingConflict(rLocator, rMetadata, !error && !bRevalidatedExists ? LandingRecordState::kAbsent : LandingRecordState::kUnverifiable);
			}
			if (revalidatedMetadata != rMetadata)
			{
				return EmitLandingConflict(rLocator, revalidatedMetadata, LandingRecordState::kReadable);
			}
			rMetadata = landing::NewLandingMetadata(rLocator, rOwner, rSession, rWorktree, iLeaseSeconds);
			if (!WriteMetadataAtomic(rLocator.path, rMetadata))
			{
				FailWindows("recover lock metadata");
				return kiExitFailure;
			}
			PrintMetadata(landing::LandingStatus(rMetadata, rLocator));
			return kiExitOk;
		}

		int HandleReleaseOrSteal(const Locator& rLocator, const nlohmann::json& rMetadata, bool bExists, LandingReleaseOperation eOperation, const std::wstring& rOwner, const std::wstring& rExpectedOwner)
		{
			if (!bExists || (eOperation == LandingReleaseOperation::kRelease && !HasOwner(rMetadata, rOwner)) || (eOperation == LandingReleaseOperation::kSteal && !HasOwner(rMetadata, rExpectedOwner)))
			{
				return EmitLandingConflict(rLocator, rMetadata, bExists ? LandingRecordState::kReadable : LandingRecordState::kAbsent);
			}

			if (eOperation == LandingReleaseOperation::kRelease)
			{
				if (::DeleteFileW(rLocator.path.c_str()) == FALSE)
				{
					FailWindows("release lock");
					return kiExitFailure;
				}
				return kiExitOk;
			}

			// steal: a lease-based landing lock is never stolen; recover is the expired-lease takeover.
			return EmitLandingConflict(rLocator, rMetadata, LandingRecordState::kReadable);
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
				return EmitLandingConflict(*locator, metadata, LandingRecordState::kAbsent);
			}
			PrintMetadata(landing::LandingStatus(metadata, *locator));
			return kiExitOk;
		}

		if (verb == L"claim")
		{
			return HandleClaim(*locator, metadata, bExists, owner, session, worktree, iLeaseSeconds);
		}

		if (verb == L"refresh")
		{
			return HandleRefresh(*locator, metadata, bExists, owner);
		}

		if (verb == L"recover")
		{
			return HandleRecover(*locator, metadata, bExists, owner, expectedOwner, session, worktree, iLeaseSeconds);
		}

		return HandleReleaseOrSteal(*locator, metadata, bExists, verb == L"release" ? LandingReleaseOperation::kRelease : LandingReleaseOperation::kSteal, owner, expectedOwner);
	}

}
