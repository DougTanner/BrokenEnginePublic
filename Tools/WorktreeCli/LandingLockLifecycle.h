#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "CoordinationStore.h"
#include "ToolCliCommon.h"

namespace toolcli::landing
{
	inline constexpr int kiLandingLeaseSchemaVersion = 3;

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

	bool IsValidLeaseDuration(int64_t iLeaseSeconds);
	nlohmann::json NewLandingMetadata(const coordination::Locator& rLocator, const std::wstring& rOwner, const std::wstring& rSession, const std::wstring& rWorktree, int64_t iLeaseSeconds);
	std::optional<LandingLease> ValidateLandingLease(const nlohmann::json& rMetadata, const coordination::Locator& rLocator, uint64_t uiCurrentTicks);
	nlohmann::json LandingStatus(const nlohmann::json& rMetadata, const coordination::Locator& rLocator);
	bool AllRegisteredWorktreesClear(const coordination::Locator& rLocator);
}
