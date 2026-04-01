#include "Game.h"

#include "Network/ClientSession.h"

namespace game
{

#if defined(BT_CLIENT)

void ClientSession::UpdateDesiredCoords(std::string_view reason)
{
	static constexpr int64_t kiMaxDesiredCoords = 8;
	engine::GridCoord desiredCoords[kiMaxDesiredCoords] {};
	int64_t iDesiredCount = 0;

	auto pushCoord = [&](engine::GridCoord coord)
	{
		ASSERT(iDesiredCount < kiMaxDesiredCoords);
		desiredCoords[iDesiredCount++] = coord;
	};

	bool bDead = gpGame->mGameFlags & engine::GameFlags::kDeathScreen;

	if (gpGame->ClientPlayerId().IsValid())
	{
		pushCoord(gpGame->mClientGridCoord);

		if constexpr (kbQuadrantNeighborSubscriptions)
		{
			if (gpGame->miQuadrantDirX != 0)
				pushCoord({.x = gpGame->mClientGridCoord.x + gpGame->miQuadrantDirX, .y = gpGame->mClientGridCoord.y});
			if (gpGame->miQuadrantDirY != 0)
				pushCoord({.x = gpGame->mClientGridCoord.x, .y = gpGame->mClientGridCoord.y + gpGame->miQuadrantDirY});
			if (gpGame->miQuadrantDirX != 0 && gpGame->miQuadrantDirY != 0)
				pushCoord({.x = gpGame->mClientGridCoord.x + gpGame->miQuadrantDirX, .y = gpGame->mClientGridCoord.y + gpGame->miQuadrantDirY});
		}
	}
	else if (bDead)
	{
		if constexpr (kbQuadrantNeighborSubscriptions)
		{
			// Death screen: keep current subscriptions + ensure origin for respawn
			for (const engine::GridCoord& rCoord : mDesiredCoords)
			{
				pushCoord(rCoord);
			}
			if (iDesiredCount > 0 && !std::ranges::contains(std::span(desiredCoords, iDesiredCount), engine::kOriginCoord))
			{
				pushCoord(engine::kOriginCoord);
			}
		}
		else
		{
			pushCoord(gpGame->mClientGridCoord);
			if (gpGame->mClientGridCoord != engine::kOriginCoord)
			{
				pushCoord(engine::kOriginCoord);
			}
		}
	}
	else
	{
		pushCoord(engine::kOriginCoord);
	}

	std::span<const engine::GridCoord> desiredSpan(desiredCoords, iDesiredCount);

	// Check if desired set changed
	bool bChanged = (iDesiredCount != std::ssize(mDesiredCoords));
	if (!bChanged)
	{
		for (int64_t i = 0; i < iDesiredCount; ++i)
		{
			if (desiredCoords[i] != mDesiredCoords.at(i))
			{
				bChanged = true;
				break;
			}
		}
	}

	if (bChanged)
	{
		// Heap: mDesiredCoords.assign and mUnwantedTimestamps may allocate on subscription changes
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

		Log(kLogNetwork, "Desired subscriptions changed Reason: {} Count: {} -> {}", reason, mDesiredCoords.size(), iDesiredCount);

		// Track when coords become unwanted for sticky subscriptions
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		for (const engine::GridCoord& rCoord : mDesiredCoords)
		{
			if (!std::ranges::contains(desiredSpan, rCoord))
			{
				Log(kLogNetwork, "  Removed ({},{})", rCoord.x, rCoord.y);
				mUnwantedTimestamps.try_emplace(rCoord, now);
			}
		}
		for (const engine::GridCoord& rCoord : desiredSpan)
		{
			if (!std::ranges::contains(mDesiredCoords, rCoord))
			{
				Log(kLogNetwork, "  Added ({},{})", rCoord.x, rCoord.y);
			}
			mUnwantedTimestamps.erase(rCoord);
		}
		mDesiredCoords.assign(desiredSpan.begin(), desiredSpan.end());
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
