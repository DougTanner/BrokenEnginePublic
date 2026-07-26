#include "HarnessLockCommands.h"

#include "CoordinationStore.h"
#include "ToolCliCommon.h"

#include <filesystem>
#include <iostream>
#include <optional>

namespace toolcli
{
	namespace
	{
		using coordination::CurrentUtcTimestamp;
		using coordination::Guard;
		using coordination::HasOwner;
		using coordination::Locator;
		using coordination::NewMetadata;
		using coordination::PrintMetadata;
		using coordination::ReadMetadata;
		using coordination::ValidateMetadataEnvelope;
		using coordination::WriteMetadataAtomic;

		std::optional<Locator> MakeHarnessLocator(const std::wstring& rKey)
		{
			std::optional<std::wstring> logicalKey = coordination::NormalizeRelativeKey(rKey);
			if (!logicalKey)
			{
				Fail("invalid harness lock key");
				return std::nullopt;
			}

			std::optional<Locator> locator = coordination::MakeLocator(L"harness", *logicalKey);
			if (!locator)
			{
				Fail("could not resolve harness lock storage");
			}
			return locator;
		}

		std::optional<Locator> ParseLocator(int iArgumentCount, wchar_t* pArgumentValues[], int iStartIndex, std::wstring& rOwner, std::wstring& rExpectedOwner, std::wstring& rSession, std::wstring& rWorktree)
		{
			std::wstring key;
			for (int i = iStartIndex; i < iArgumentCount; ++i)
			{
				std::wstring_view argument = pArgumentValues[i];
				std::wstring* pDestination = nullptr;
				if (argument == L"--key")
				{
					pDestination = &key;
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
			if (key.empty())
			{
				Fail("harness lock requires --key");
				return std::nullopt;
			}
			return MakeHarnessLocator(key);
		}

		bool StampHarnessHeartbeat(const std::filesystem::path& rPath, nlohmann::json& rMetadata)
		{
			rMetadata["heartbeatAt"] = CurrentUtcTimestamp();
			rMetadata["heartbeatPid"] = ::GetCurrentProcessId();
			return WriteMetadataAtomic(rPath, rMetadata);
		}

	}

	int RunHarnessLockCommand(int iArgumentCount, wchar_t* pArgumentValues[])
	{
		if (iArgumentCount < 3)
		{
			Fail("lock requires token, claim, status, release, steal, or heartbeat");
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
			return PrintOwnerToken();
		}
		if (verb != L"claim" && verb != L"status" && verb != L"release" && verb != L"steal" && verb != L"heartbeat")
		{
			Fail("unknown lock verb");
			return kiExitFailure;
		}

		std::wstring owner;
		std::wstring expectedOwner;
		std::wstring session;
		std::wstring worktree;
		std::optional<Locator> locator = ParseLocator(iArgumentCount, pArgumentValues, 3, owner, expectedOwner, session, worktree);
		if (!locator)
		{
			return kiExitFailure;
		}
		if ((verb == L"claim" || verb == L"steal") && (owner.empty() || session.empty() || worktree.empty()))
		{
			Fail("claim and steal require --owner, --session, and --worktree");
			return kiExitFailure;
		}
		if ((verb == L"release" || verb == L"heartbeat") && owner.empty())
		{
			Fail("release and heartbeat require --owner");
			return kiExitFailure;
		}
		if (verb == L"steal" && expectedOwner.empty())
		{
			Fail("steal requires --expect");
			return kiExitFailure;
		}

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

		std::error_code error;
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
		if (bExists && !ValidateMetadataEnvelope(metadata, *locator))
		{
			Fail("lock metadata envelope is invalid");
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
		if (!bExists || ((verb == L"release" || verb == L"heartbeat") && !HasOwner(metadata, owner)) || (verb == L"steal" && !HasOwner(metadata, expectedOwner)))
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
		if (verb == L"heartbeat")
		{
			if (!StampHarnessHeartbeat(locator->path, metadata))
			{
				FailWindows("write lock metadata");
				return kiExitFailure;
			}
			PrintMetadata(metadata);
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
		std::optional<Locator> locator = MakeHarnessLocator(L"default");
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
		if (!ReadMetadata(locator->path, metadata) || !ValidateMetadataEnvelope(metadata, *locator) || !HasOwner(metadata, rOwner))
		{
			return false;
		}
		return StampHarnessHeartbeat(locator->path, metadata);
	}
}
