#include "Network/Client/ClientSession.h"

#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Game.h"

namespace game
{

#if defined(BT_CLIENT)

void ClientSession::ApplyReceivedStaticData()
{
	// Heap: try_emplace may insert new CoordFrames, NavData vectors moved into staticData
	ScopedSuppressAllocationTracking suppress;

	std::vector<engine::ReceivedStaticData>& rStaticDataList = mpRuntime->mpClient->mReceivedStaticData;
	for (engine::ReceivedStaticData& rReceived : rStaticDataList)
	{
		engine::CoordFrames& rFrames = gpGame->mCoordFrames.try_emplace(rReceived.coord).first->second;
		rFrames.staticData = std::move(rReceived.staticData);
		rFrames.staticData.coord = rReceived.coord;

		// Subscription-driven island texture loading. AcquireTextureSlot is idempotent; duplicate
		// CRCs across placements short-circuit on the hot path. Slot mint + chunk-load request
		// happens here so the data is in-flight before UpdateActiveIslands references the slot.
		for (const engine::IslandPlacement& rPlacement : rFrames.staticData.islands)
		{
			engine::gpIslandTerrain->AcquireTextureSlot(rPlacement.islandCrc);
		}
	}
}

void ClientSession::ApplyReceivedFullStates()
{
	// Heap: try_emplace may insert new CoordFrames; full state is moved directly into snapshot ring slot 0
	ScopedSuppressAllocationTracking suppress;

	std::vector<engine::ReceivedCoordFullState>& rFullStates = mpRuntime->mpClient->mReceivedFullStates;
	if (rFullStates.empty())
	{
		return;
	}

	for (engine::ReceivedCoordFullState& rFullState : rFullStates)
	{
		engine::GridCoord coord = rFullState.coord;
		int64_t iTick = rFullState.iTick;

		engine::CoordFrames& rCoordFrames = gpGame->mCoordFrames.try_emplace(coord).first->second;

		// Initialize client-only objects
		Frame& rFrame = *rFullState.pFrame;
		BlastersInterpolate::ClientInitAll(rFrame);
		MissilesInterpolate::ClientInitAll(rFrame);
		SpaceshipsInterpolate::ClientInitAll(rFrame);

		// Copy smoke trail smoothed positions from the most recent ring frame to preserve
		// rendering continuity across reconciliation.
		if (rCoordFrames.iSnapshotCount > 0)
		{
			int64_t iTailPhysical = engine::SnapshotIndex(rCoordFrames.iSnapshotHead, rCoordFrames.iSnapshotCount - 1);
			const std::unique_ptr<Frame>& pTail = rCoordFrames.snapshots[iTailPhysical];
			if (pTail != nullptr)
			{
				const engine::SmokeTrailsInterpolate& rOldSmokeTrails = pTail->interpolate.smokeTrails;
				engine::SmokeTrailsInterpolate& rNewSmokeTrails = rFrame.interpolate.smokeTrails;
				int64_t iCopyCount = std::min(rOldSmokeTrails.iCount, rNewSmokeTrails.iCount);
				if (iCopyCount > 0)
				{
					std::memcpy(rNewSmokeTrails.pVecSmoothedPositions, rOldSmokeTrails.pVecSmoothedPositions, iCopyCount * sizeof(XMVECTOR));
				}
			}
		}

		if (rCoordFrames.iConfirmedTick < 0)
		{
			// Only advance tick counter during initial setup (no other coords have confirmed data yet)
			bool bInitialSetup = (GetConfirmedTick() < 0);

			// Move the received full state into the ring as the confirmed frame.
			rCoordFrames.iSnapshotHead = 0;
			rCoordFrames.snapshots[0] = std::move(rFullState.pFrame);
			rCoordFrames.snapshots[0]->postRender.sharedCrc = rCoordFrames.snapshots[0]->Crcs();
			rCoordFrames.iSnapshotCount = 1;
			rCoordFrames.iConfirmedTick = iTick;
			rCoordFrames.iLastFullStateTick = iTick;
			rCoordFrames.iConfirmedOffset = 0;

			float fFullStateTime = rCoordFrames.snapshots[0]->interpolate.fCurrentTime;

			// Set frame counter from first received full state only (not from subsequent neighbor subscriptions).
			// Offset sim tick back by the jitter-safety floor so sim starts BEHIND latestServerTick, matching
			// the steady-state target computed by ComputeClockCorrectionNs. Avoids a ~150 ms freeze while the
			// ceiling clamp waits for latest to catch up and drains the spurious +targetBehind error.
			// Clamp the offset at iTick so a fresh post-load server (iTick < jitter-safety floor) does
			// not produce a negative sim tick.
			if (bInitialSetup && gpGame->TickCounter() < iTick)
			{
				static constexpr int64_t kiTickTimeMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(kTickNs).count();
				static constexpr int64_t kiInitialTargetBehind = (engine::kiJitterSafetyUs + kiTickTimeMicroseconds - 1) / kiTickTimeMicroseconds;
				const int64_t iAppliedBehind = std::min<int64_t>(kiInitialTargetBehind, iTick);
				gpGame->SetTickCounter(iTick - iAppliedBehind);
				gpGame->SetCurrentTime(fFullStateTime - static_cast<float>(iAppliedBehind) * kfDeltaTime);
				gpGame->ResetRenderClock();
			}
		}
		else
		{
			// Reject stale full states: tick must be after confirmed tick
			if (iTick <= rCoordFrames.iConfirmedTick)
			{
				LOG(kNetwork, kVerbose, "ApplyReceivedFullStates Rejected stale full state Coord: ({},{}) FullStateTick: {} ConfirmedTick: {}", coord.x, coord.y, iTick, rCoordFrames.iConfirmedTick);
				continue;
			}

			// Coord already has confirmed state: store as pending for reconcile injection
			rCoordFrames.pendingFullState = engine::CoordFrames::PendingFullState {
				.iTick = iTick,
				.pFrame = std::move(rFullState.pFrame),
			};
		}
	}
}

bool ClientSession::ApplyReceivedUpdates()
{
	// Heap: map insertion for per-frame server updates
	ScopedSuppressAllocationTracking suppress;

	bool bHasNewData = false;
	const std::vector<engine::ClientCoordSlot>& rCoordSlots = mpRuntime->mpClient->mCoordSlots;
	std::vector<std::vector<engine::ReceivedCoordUpdate>>& rAllUpdates = mpRuntime->mpClient->mReceivedCoordUpdates;

	for (int64_t iSlot = 0; iSlot < std::ssize(rCoordSlots); ++iSlot)
	{
		std::vector<engine::ReceivedCoordUpdate>& rSlotUpdates = rAllUpdates.at(iSlot);
		if (rSlotUpdates.empty())
		{
			continue;
		}

		const engine::ClientCoordSlot& rSlot = rCoordSlots.at(iSlot);
		if (rSlot.eState != engine::CoordSubscriptionState::kActive)
		{
			rSlotUpdates.clear();
			continue;
		}

		engine::GridCoord coord = rSlot.coord;
		engine::CoordFrames& rCoordFrames = gpGame->mCoordFrames.at(coord);

		for (engine::ReceivedCoordUpdate& rUpdate : rSlotUpdates)
		{
			if (rUpdate.iTick <= rCoordFrames.iConfirmedTick)
			{
				continue;
			}

			mpRuntime->miLatestServerTick = std::max(mpRuntime->miLatestServerTick, rUpdate.iTick);

			if (static_cast<int64_t>(rCoordFrames.serverUpdates.size()) >= engine::kiMaxBufferedFrames)
			{
				LOG(kNetwork, kWarning, "ClientSession::ApplyReceivedUpdates Buffer full Coord: ({},{}) Size: {} Tick: {}", coord.x, coord.y, rCoordFrames.serverUpdates.size(), rUpdate.iTick);
				// Recoverable (slow client / server burst): drop the update and keep the session alive
				DEBUG_BREAK();
				bHasNewData = true;
				continue;
			}

			bool bInserted = rCoordFrames.serverUpdates.try_emplace(rUpdate.iTick, engine::CoordFrames::CoordServerUpdate {
				.sharedCrc = rUpdate.sharedCrc,
				.statusChanges = std::move(rUpdate.statusChanges),
			}).second;
			if (bInserted)
			{
				bHasNewData = true;
			}
		}

		rSlotUpdates.clear();
	}

	return bHasNewData;
}

int64_t ClientSession::GetConfirmedTick() const
{
	int64_t iMinimumTick = -1;
	for (const auto& [rCoord, rCoordFrames] : gpGame->mCoordFrames)
	{
		if (rCoordFrames.iConfirmedTick >= 0 && (iMinimumTick < 0 || rCoordFrames.iConfirmedTick < iMinimumTick))
		{
			iMinimumTick = rCoordFrames.iConfirmedTick;
		}
	}
	return iMinimumTick;
}

int64_t ClientSession::GetClientConfirmedTick() const
{
	auto it = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
	if (it == gpGame->mCoordFrames.end() || it->second.iConfirmedTick < 0)
	{
		return -1;
	}
	return it->second.iConfirmedTick;
}

int64_t ClientSession::GetServerUpdateBufferSize() const
{
	int64_t iTotal = 0;
	for (const auto& [rCoord, rCoordFrames] : gpGame->mCoordFrames)
	{
		if (rCoordFrames.iConfirmedTick >= 0)
		{
			iTotal += static_cast<int64_t>(rCoordFrames.serverUpdates.size());
		}
	}
	return iTotal;
}

#endif // BT_CLIENT

} // namespace game
