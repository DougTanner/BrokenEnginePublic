#include "Game.h"

#include "Network/ClientSession.h"

namespace game
{

#if defined(BT_CLIENT)

void ClientSession::UpdateDesiredCoords(std::string_view reason)
{
	std::vector<engine::GridCoord> desiredCoords;
	desiredCoords.reserve(5);

	bool bDead = gpGame->mGameFlags & engine::GameFlags::kDeathScreen;

	if (gpGame->HumanPlayerId().IsValid())
	{
		desiredCoords.push_back(gpGame->mHumanGridCoord);

		if constexpr (kbEnableQuadrantNeighborSubscriptions)
		{
			if (gpGame->miQuadrantDirX != 0)
				desiredCoords.push_back({.x = gpGame->mHumanGridCoord.x + gpGame->miQuadrantDirX, .y = gpGame->mHumanGridCoord.y});
			if (gpGame->miQuadrantDirY != 0)
				desiredCoords.push_back({.x = gpGame->mHumanGridCoord.x, .y = gpGame->mHumanGridCoord.y + gpGame->miQuadrantDirY});
			if (gpGame->miQuadrantDirX != 0 && gpGame->miQuadrantDirY != 0)
				desiredCoords.push_back({.x = gpGame->mHumanGridCoord.x + gpGame->miQuadrantDirX, .y = gpGame->mHumanGridCoord.y + gpGame->miQuadrantDirY});
		}
	}
	else if (bDead)
	{
		if constexpr (kbEnableQuadrantNeighborSubscriptions)
		{
			// Death screen: keep current subscriptions + ensure origin for respawn
			desiredCoords = mDesiredCoords;
			if (!desiredCoords.empty() && !std::ranges::contains(desiredCoords, engine::kOriginCoord))
			{
				desiredCoords.push_back(engine::kOriginCoord);
			}
		}
		else
		{
			desiredCoords.push_back(gpGame->mHumanGridCoord);
			if (gpGame->mHumanGridCoord != engine::kOriginCoord)
			{
				desiredCoords.push_back(engine::kOriginCoord);
			}
		}
	}
	else
	{
		desiredCoords.push_back(engine::kOriginCoord);
	}

	if (desiredCoords != mDesiredCoords)
	{
		Log(kLogNetwork, "Desired subscriptions changed Reason: {} Count: {} -> {}", reason, mDesiredCoords.size(), desiredCoords.size());

		// Track when coords become unwanted for sticky subscriptions
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		for (const engine::GridCoord& rCoord : mDesiredCoords)
		{
			if (!std::ranges::contains(desiredCoords, rCoord))
			{
				Log(kLogNetwork, "  Removed ({},{})", rCoord.x, rCoord.y);
				mUnwantedTimestamps.try_emplace(rCoord, now);
			}
		}
		for (const engine::GridCoord& rCoord : desiredCoords)
		{
			if (!std::ranges::contains(mDesiredCoords, rCoord))
			{
				Log(kLogNetwork, "  Added ({},{})", rCoord.x, rCoord.y);
			}
			mUnwantedTimestamps.erase(rCoord);
		}
		mDesiredCoords = desiredCoords;
	}
}

void ClientSession::UpdateSubscriptions()
{
	if (mpClientNetwork == nullptr)
	{
		return;
	}

	// Heap: vector operations for subscription queue
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Build effective desired list: fresh desired + unexpired sticky coords
	std::vector<engine::GridCoord> effectiveDesired = mDesiredCoords;
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	std::erase_if(mUnwantedTimestamps, [&](const std::pair<const engine::GridCoord, std::chrono::steady_clock::time_point>& rPair)
	{
		if (now - rPair.second >= kStickySubscriptionDuration)
		{
			Log(kLogNetwork, "Sticky subscription expired ({},{})", rPair.first.x, rPair.first.y);
			return true;
		}
		effectiveDesired.push_back(rPair.first);
		return false;
	});

	UnsubscribeStaleCoords(effectiveDesired);
	BuildSubscriptionQueue(effectiveDesired);
	TrySubscribeNext();
}

#endif // BT_CLIENT

} // namespace game
