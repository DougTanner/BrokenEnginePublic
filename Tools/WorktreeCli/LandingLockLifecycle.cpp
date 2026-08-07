#include "LandingLockLifecycle.h"

#include "ToolCliCommon.h"

#include <filesystem>
#include <limits>
#include <vector>

namespace toolcli::landing
{
	using coordination::CurrentUtcTicks;
	using coordination::FormatUtcTimestamp;
	using coordination::JsonInt64;
	using coordination::Locator;
	using coordination::NewMetadata;
	using coordination::ParseUtcTimestamp;

	namespace
	{
		constexpr int64_t kiMinimumLeaseSeconds = 60;
		constexpr int64_t kiMaximumLeaseSeconds = 86'400;
	}

	bool IsValidLeaseDuration(int64_t iLeaseSeconds)
	{
		return iLeaseSeconds >= kiMinimumLeaseSeconds && iLeaseSeconds <= kiMaximumLeaseSeconds;
	}

	nlohmann::json NewLandingMetadata(const Locator& rLocator, const std::wstring& rOwner, const std::wstring& rSession, const std::wstring& rWorktree, int64_t iLeaseSeconds)
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

	std::optional<LandingLease> ValidateLandingLease(const nlohmann::json& rMetadata, const Locator& rLocator, uint64_t uiCurrentTicks)
	{
		if (!coordination::ValidateMetadataEnvelope(rMetadata, rLocator, kiLandingLeaseSchemaVersion))
		{
			return std::nullopt;
		}
		if (!rMetadata.contains("expiresAt") || !rMetadata["expiresAt"].is_string() || rMetadata["expiresAt"].get<std::string>().empty())
		{
			return std::nullopt;
		}
		if (!rMetadata.contains("leaseDurationSeconds"))
		{
			return std::nullopt;
		}
		const std::optional<int64_t> durationSeconds = JsonInt64(rMetadata["leaseDurationSeconds"]);
		if (!durationSeconds || !IsValidLeaseDuration(*durationSeconds))
		{
			return std::nullopt;
		}
		LandingLease lease;
		lease.owner = rMetadata["owner"].get<std::string>();
		lease.claimedAt = rMetadata["claimedAt"].get<std::string>();
		lease.heartbeatAt = rMetadata["heartbeatAt"].get<std::string>();
		lease.expiresAt = rMetadata["expiresAt"].get<std::string>();
		lease.iDurationSeconds = *durationSeconds;
		// The envelope already proved claimedAt and heartbeatAt parse and are ordered; these calls exist to fill the lease ticks.
		if (!ParseUtcTimestamp(lease.claimedAt, lease.uiClaimedTicks) || !ParseUtcTimestamp(lease.heartbeatAt, lease.uiHeartbeatTicks) || !ParseUtcTimestamp(lease.expiresAt, lease.uiExpiresTicks))
		{
			return std::nullopt;
		}
		if (lease.uiHeartbeatTicks > (std::numeric_limits<uint64_t>::max)() - static_cast<uint64_t>(lease.iDurationSeconds) * 10'000'000ull ||
			lease.uiExpiresTicks != lease.uiHeartbeatTicks + static_cast<uint64_t>(lease.iDurationSeconds) * 10'000'000ull)
		{
			return std::nullopt;
		}
		if (lease.uiHeartbeatTicks > uiCurrentTicks)
		{
			return std::nullopt;
		}
		return lease;
	}

	nlohmann::json LandingStatus(const nlohmann::json& rMetadata, const Locator& rLocator)
	{
		nlohmann::json status = { { "held", true }, { "leaseState", "unverifiable" } };
		for (const char* pField : { "domain", "logicalKey", "owner", "session", "worktree", "claimedAt", "heartbeatAt", "expiresAt" })
		{
			if (rMetadata.contains(pField) && rMetadata[pField].is_string())
			{
				status[pField] = rMetadata[pField];
			}
		}
		for (const char* pField : { "schemaVersion", "leaseDurationSeconds" })
		{
			if (rMetadata.contains(pField) && rMetadata[pField].is_number_integer())
			{
				status[pField] = rMetadata[pField];
			}
		}
		const uint64_t uiCurrentTicks = CurrentUtcTicks();
		std::optional<LandingLease> lease = ValidateLandingLease(rMetadata, rLocator, uiCurrentTicks);
		if (lease)
		{
			status["leaseState"] = uiCurrentTicks < lease->uiExpiresTicks ? "live" : "expired";
		}
		return status;
	}

	bool AllRegisteredWorktreesClear(const Locator& rLocator)
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
}
