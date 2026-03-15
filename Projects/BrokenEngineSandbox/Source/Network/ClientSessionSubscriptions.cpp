#include "Game.h"

#include "Network/ClientSession.h"
#include "Frame/Collections/Players/Players.h"

namespace game
{

#if defined(BT_CLIENT)

std::vector<engine::GridCoord> ClientSession::ComputeDesiredCoords() const
{
	std::vector<engine::GridCoord> desiredCoords;
	desiredCoords.reserve(5);

	bool bDead = gpGame->mGameFlags & engine::GameFlags::kDeathScreen;

	if (gpGame->HumanPlayerId().IsValid())
	{
		desiredCoords.push_back(gpGame->mHumanGridCoord);

		// Add quadrant neighbor coords for seamless cross-cell gameplay
		if constexpr (kbEnableQuadrantNeighborSubscriptions)
		{
			if (gpGame->mCoordFrames.contains(gpGame->mHumanGridCoord))
			{
				const Frame& rFrame = gpGame->CurrentFrame(gpGame->mHumanGridCoord);
				auto it = rFrame.interpolate.pPlayers->idToIndexMap.find(gpGame->HumanPlayerId());
				if (it != rFrame.interpolate.pPlayers->idToIndexMap.end())
				{
					engine::GridCoord quadrantOffsets[3];
					quadrantOffsets[0] = {.x = gpGame->miQuadrantDirX, .y = 0};
					quadrantOffsets[1] = {.x = 0, .y = gpGame->miQuadrantDirY};
					quadrantOffsets[2] = {.x = gpGame->miQuadrantDirX, .y = gpGame->miQuadrantDirY};

					for (const engine::GridCoord& rOffset : quadrantOffsets)
					{
						desiredCoords.push_back({gpGame->mHumanGridCoord.x + rOffset.x, gpGame->mHumanGridCoord.y + rOffset.y});
					}
				}
			}
		}
	}
	else if (bDead)
	{
		// Dead: keep death coord + pre-emptive origin for respawn
		desiredCoords.push_back(gpGame->mHumanGridCoord);
		if (gpGame->mHumanGridCoord != engine::kOriginCoord)
		{
			desiredCoords.push_back(engine::kOriginCoord);
		}
	}
	else
	{
		// Not yet assigned: just origin
		desiredCoords.push_back(engine::kOriginCoord);
	}

	return desiredCoords;
}

void ClientSession::UpdateSubscriptions()
{
	if (mpClientNetwork == nullptr)
	{
		return;
	}

	// Player alive but not yet in assigned cell (mid-transfer):
	// don't recompute desired coords, but allow pending subscriptions to proceed
	bool bMidTransfer = gpGame->HumanPlayerId().IsValid()
		&& gpGame->mCoordFrames.contains(gpGame->mHumanGridCoord)
		&& gpGame->mCoordFrames.at(gpGame->mHumanGridCoord).iConfirmedTick >= 0
		&& !gpGame->CurrentFrame(gpGame->mHumanGridCoord).interpolate.pPlayers->idToIndexMap.contains(gpGame->HumanPlayerId());

	if (!bMidTransfer)
	{
		// Heap: vector operations for subscription queue
		ScopedSuppressAllocationTracking suppressAllocationTracking;

		std::vector<engine::GridCoord> desiredCoords = ComputeDesiredCoords();
		UnsubscribeStaleCoords(desiredCoords);
		BuildSubscriptionQueue(desiredCoords);
	}

	TrySubscribeNext();
}

#endif // BT_CLIENT

} // namespace game
