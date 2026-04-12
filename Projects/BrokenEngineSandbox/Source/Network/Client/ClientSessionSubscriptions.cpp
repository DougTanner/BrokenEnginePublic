#include "Game.h"

#include "Network/Client/ClientSession.h"

namespace game
{

#if defined(BT_CLIENT)

void ClientSession::UpdateDesiredCoords(SubscriptionChangeReason eReason)
{
	static constexpr int64_t kiMaxDesiredCoords = 8;
	engine::GridCoord desiredCoords[kiMaxDesiredCoords] {};
	int64_t iDesiredCount = 0;

	auto pushCoord = [&](engine::GridCoord coord)
	{
		ASSERT(iDesiredCount < kiMaxDesiredCoords);
		desiredCoords[iDesiredCount++] = coord;
	};

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
	else
	{
		// No focused player: subscribe to origin for fleet spawn requests
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

		common::ScopedWorkbufferBuilder message(common::gpThreadLocal->mWorkbuffer);
		message.Append("Desired subscriptions changed Reason: ");
		message.Append(ToString(eReason));
		message.Append(" Removed: [");

		auto appendCoord = [&message](bool& rbFirst, engine::GridCoord coord)
		{
			if (!rbFirst)
			{
				message.Append(",");
			}
			rbFirst = false;
			message.Append("(");
			message.Append(static_cast<int64_t>(coord.x));
			message.Append(",");
			message.Append(static_cast<int64_t>(coord.y));
			message.Append(")");
		};

		// Track when coords become unwanted for sticky subscriptions; build removed list
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		bool bFirstRemoved = true;
		for (const engine::GridCoord& rCoord : mDesiredCoords)
		{
			if (!std::ranges::contains(desiredSpan, rCoord))
			{
				mUnwantedTimestamps.try_emplace(rCoord, now);
				appendCoord(bFirstRemoved, rCoord);
			}
		}
		message.Append("] Added: [");
		// Build added list; clear sticky timestamps for re-wanted coords
		bool bFirstAdded = true;
		for (const engine::GridCoord& rCoord : desiredSpan)
		{
			if (!std::ranges::contains(mDesiredCoords, rCoord))
			{
				appendCoord(bFirstAdded, rCoord);
			}
			mUnwantedTimestamps.erase(rCoord);
		}
		message.Append("] Tick: ");
		message.Append(gpGame->TickCounter());

		LOG(kNetwork, kVerbose, "{}", message);

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
			LOG(kNetwork, kVerbose, "Sticky subscription expired ({},{})", rPair.first.x, rPair.first.y);
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
