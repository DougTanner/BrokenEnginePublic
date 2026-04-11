#include "Network/Client/ClientDataReceiver.h"

#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Game.h"
#include "Network/Client/ClientSession.h"

namespace game
{

#if defined(BT_CLIENT)

void ClientDataReceiver::ApplyReceivedStaticData()
{
	// Heap: try_emplace may insert new CoordFrames, NavData vectors moved into staticData
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<engine::ReceivedStaticData>& rStaticDataList = gpClientSession->mpClientNetwork->DrainReceivedStaticData();
	for (engine::ReceivedStaticData& rReceived : rStaticDataList)
	{
		engine::CoordFrames& rFrames = gpGame->mCoordFrames.try_emplace(rReceived.coord).first->second;
		rFrames.staticData = std::move(rReceived.staticData);
		rFrames.staticData.coord = rReceived.coord;
	}
}

void ClientDataReceiver::ApplyReceivedFullStates()
{
	// Heap: Moving unique_ptr<Frame> into mCurrentFrames, stringstream serialization
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<engine::ReceivedCoordFullState>& rFullStates = gpClientSession->mpClientNetwork->DrainReceivedFullStates();
	if (rFullStates.empty())
	{
		return;
	}

	for (engine::ReceivedCoordFullState& rFullState : rFullStates)
	{
		engine::GridCoord coord = rFullState.coord;
		int64_t iTick = rFullState.iTick;

		engine::CoordFrames& rSub = gpGame->mCoordFrames.try_emplace(coord).first->second;

		// Initialize client-only objects
		Frame& rFrame = *rFullState.pFrame;
		BlastersInterpolate::ClientInitAll(rFrame);
		MissilesInterpolate::ClientInitAll(rFrame);
		SpaceshipsInterpolate::ClientInitAll(rFrame);

		// Copy smoke trail smoothed positions from existing frame to preserve rendering continuity across reconciliation
		if (rSub.pCurrent != nullptr)
		{
			const engine::SmokeTrailsInterpolate& rOldSmokeTrails = rSub.pCurrent->interpolate.smokeTrails;
			engine::SmokeTrailsInterpolate& rNewSmokeTrails = rFrame.interpolate.smokeTrails;
			int64_t iCopyCount = std::min(rOldSmokeTrails.iCount, rNewSmokeTrails.iCount);
			if (iCopyCount > 0)
			{
				std::memcpy(rNewSmokeTrails.pVecSmoothedPositions, rOldSmokeTrails.pVecSmoothedPositions, iCopyCount * sizeof(XMVECTOR));
			}
		}
		if (rSub.uiGeneration == 0)
		{
			rSub.uiGeneration = gpClientSession->mpReconciler->NextGeneration();
		}

		if (rSub.iConfirmedTick < 0)
		{
			// Only advance tick counter during initial setup (no other coords have confirmed data yet)
			bool bInitialSetup = (gpClientSession->GetConfirmedTick() < 0);

			// Single serialization copy: received -> current
			rSub.pCurrent = std::make_unique<Frame>();
			std::ostringstream outputStream;
			outputStream << *rFullState.pFrame;
			std::istringstream inputStream(outputStream.str());
			inputStream >> *rSub.pCurrent;

			if (rSub.pNext == nullptr)
			{
				rSub.pNext = std::make_unique<Frame>();
			}

			// Original received frame -> snapshot[0], which IS the confirmed frame
			rSub.iSnapshotHead = 0;
			rSub.snapshots[0] = std::move(rFullState.pFrame);
			rSub.snapshots[0]->postRender.sharedCrc = rSub.snapshots[0]->Crcs();
			rSub.iSnapshotCount = 1;
			rSub.iConfirmedTick = iTick;
			rSub.iConfirmedOffset = 0;

			// Set frame counter from first received full state only (not from subsequent neighbor subscriptions)
			if (bInitialSetup && gpGame->TickCounter() < iTick)
			{
				gpGame->SetTickCounter(iTick);
				gpGame->SetCurrentTime(rSub.pCurrent->interpolate.fCurrentTime);
			}

			ConfirmedClientState confirmedState;
			confirmedState.clientGridCoord = gpGame->mClientGridCoord;
			confirmedState.clientGlobalPlayerId = gpGame->ClientPlayerId();
			confirmedState.fPreviousClientArmor = gpGame->PreviousClientArmor();
			confirmedState.fCurrentTime = rSub.pCurrent->interpolate.fCurrentTime;
			gpClientSession->mpReconciler->InitConfirmedClientState(confirmedState);
			gpClientSession->mpReconciler->SetHasNewData();
		}
		else
		{
			// Reject stale full states: tick must be after confirmed tick
			if (iTick <= rSub.iConfirmedTick)
			{
				LOG(kNetwork, kVerbose, "ApplyReceivedFullStates Rejected stale full state Coord: ({},{}) FullStateTick: {} ConfirmedTick: {}", coord.x, coord.y, iTick, rSub.iConfirmedTick);
				continue;
			}

			// Coord already has confirmed state: store as pending for reconcile injection
			rSub.pendingFullState = engine::CoordFrames::PendingFullState {
				.iTick = iTick,
				.pFrame = std::move(rFullState.pFrame),
			};

			gpClientSession->mpReconciler->SetHasNewData();
		}
	}
}

void ClientDataReceiver::ApplyReceivedUpdates()
{
	if (gpClientSession->ApplyReceivedUpdatesBase())
	{
		gpClientSession->mpReconciler->SetHasNewData();
	}
}

#endif // BT_CLIENT

} // namespace game
