#include "Game.h"

namespace game
{

#ifdef BT_CLIENT

void Game::KickReconcile()
{
	mpReconcileContext = std::make_unique<ReconcileContext>();
	ReconcileContext& rReconcileContext = *mpReconcileContext;

	// Populate per-coord work items
	for (auto& [rCoord, rState] : mCoordReconcileStates)
	{
		if (rState.iConfirmedFrame < 0)
		{
			continue;
		}

		CoordReconcileWork work;
		work.coord = rCoord;
		work.uiGeneration = rState.uiGeneration;
		work.iConfirmedFrame = rState.iConfirmedFrame;
		work.confirmedSerializedFrame = rState.confirmedSerializedFrame;
		work.serverUpdates = std::move(rState.serverUpdates);
		work.extrapolatedSnapshots = std::move(rState.extrapolatedSnapshots);
		work.pendingFullState = std::move(rState.pendingFullState);

		rState.serverUpdates.clear();
		rState.extrapolatedSnapshots.clear();
		rState.pendingFullState.reset();

		rReconcileContext.coordWork.push_back(std::move(work));
	}

	// Global input
	rReconcileContext.confirmedHumanState = mConfirmedHumanState;
	rReconcileContext.uiNextFrameId = muiNextFrameId;
	rReconcileContext.iTargetFrame = miFrameCounter;
	rReconcileContext.playerAlignment = mPlayerAlignment;

	// Dispatch to worker
	mpReconcileWorker->Wake([this]()
	{
		Reconcile(*mpReconcileContext, mAlignments);
	});
}

std::pair<bool, int64_t> Game::ReconcileCrcFastPath(ReconcileContext& rReconcileContext)
{
	bool bAllHandled = true;
	int64_t iMinConfirmedFrame = std::numeric_limits<int64_t>::max();

	int64_t iOldMinConfirmed = std::numeric_limits<int64_t>::max();
	for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.iConfirmedFrame >= 0 && rWork.iConfirmedFrame < iOldMinConfirmed)
		{
			iOldMinConfirmed = rWork.iConfirmedFrame;
		}
	}

	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.serverUpdates.empty() && !rWork.pendingFullState.has_value())
		{
			if (rWork.iConfirmedFrame >= 0 && rWork.iConfirmedFrame < iMinConfirmedFrame)
			{
				iMinConfirmedFrame = rWork.iConfirmedFrame;
			}
			continue;
		}

		if (rWork.pendingFullState.has_value())
		{
			bAllHandled = false;
			if (rWork.iConfirmedFrame >= 0 && rWork.iConfirmedFrame < iMinConfirmedFrame)
			{
				iMinConfirmedFrame = rWork.iConfirmedFrame;
			}
			continue;
		}

		// Check CRC fast-path for this coord
		int64_t iExpected = rWork.iConfirmedFrame + 1;
		bool bMatch = true;
		int64_t iLastMatched = -1;
		auto it = rWork.serverUpdates.begin();

		while (it != rWork.serverUpdates.end() && it->first == iExpected)
		{
			auto snapIt = rWork.extrapolatedSnapshots.find(iExpected);
			if (snapIt == rWork.extrapolatedSnapshots.end() || snapIt->second.crc != it->second.serverCrc || snapIt->second.inputCrc != it->second.inputCrc)
			{
				bMatch = false;
				break;
			}
			iLastMatched = iExpected;
			++iExpected;
			++it;
		}

		if (bMatch && iLastMatched >= 0)
		{
			rWork.bCrcFastPath = true;
			rWork.iNewConfirmedFrame = iLastMatched;
			rWork.newConfirmedSerializedFrame = std::move(rWork.extrapolatedSnapshots.at(iLastMatched).serializedFrame);

			// Keep snapshots beyond matched frame
			for (auto snapIt = rWork.extrapolatedSnapshots.begin(); snapIt != rWork.extrapolatedSnapshots.end(); )
			{
				if (snapIt->first <= iLastMatched)
				{
					snapIt = rWork.extrapolatedSnapshots.erase(snapIt);
				}
				else
				{
					++snapIt;
				}
			}
			rWork.newExtrapolatedSnapshots = std::move(rWork.extrapolatedSnapshots);

			if (rWork.iConfirmedFrame < iMinConfirmedFrame)
			{
				iMinConfirmedFrame = rWork.iConfirmedFrame;
			}
		}
		else
		{
			bAllHandled = false;
			if (rWork.iConfirmedFrame >= 0 && rWork.iConfirmedFrame < iMinConfirmedFrame)
			{
				iMinConfirmedFrame = rWork.iConfirmedFrame;
			}
		}
	}

	if (bAllHandled)
	{
		rReconcileContext.bCrcFastPathHandledAll = true;
		rReconcileContext.newConfirmedHumanState = rReconcileContext.confirmedHumanState;
		rReconcileContext.humanGridCoord = rReconcileContext.confirmedHumanState.humanGridCoord;
		rReconcileContext.humanPlayerId = rReconcileContext.confirmedHumanState.humanPlayerId;
		rReconcileContext.fPreviousHumanArmor = rReconcileContext.confirmedHumanState.fPreviousHumanArmor;
		if (iOldMinConfirmed != std::numeric_limits<int64_t>::max() && iMinConfirmedFrame > iOldMinConfirmed)
		{
			for (int64_t i = 0; i < iMinConfirmedFrame - iOldMinConfirmed; ++i)
			{
				rReconcileContext.newConfirmedHumanState.fCurrentTime += kfDeltaTime;
			}
		}
	}

	return {bAllHandled, iMinConfirmedFrame};
}

void Game::Reconcile(ReconcileContext& rReconcileContext, [[maybe_unused]] const engine::Alignments& rAlignments)
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	auto [bAllHandled, iMinConfirmedFrame] = ReconcileCrcFastPath(rReconcileContext);
	if (bAllHandled)
	{
		return;
	}

	// Build coord-to-index map for O(1) lookups
	std::unordered_map<engine::GridCoord, size_t> coordWorkIndex;
	for (size_t i = 0; i < rReconcileContext.coordWork.size(); ++i)
	{
		coordWorkIndex[rReconcileContext.coordWork.at(i).coord] = i;
	}

	ReconcileRollback(rReconcileContext, iMinConfirmedFrame);

	int64_t iMaxConsecutive = ReconcileFindReplayRange(rReconcileContext, iMinConfirmedFrame);

	ReconcileReplay(rReconcileContext, iMinConfirmedFrame, iMaxConsecutive, coordWorkIndex);
	if (rReconcileContext.iDesyncFrame >= 0)
	{
		return;
	}

	ReconcilePruneInactiveFrames(rReconcileContext);

	// Save confirmed state
	rReconcileContext.newConfirmedHumanState.humanGridCoord = rReconcileContext.humanGridCoord;
	rReconcileContext.newConfirmedHumanState.humanPlayerId = rReconcileContext.humanPlayerId;
	rReconcileContext.newConfirmedHumanState.fPreviousHumanArmor = rReconcileContext.fPreviousHumanArmor;
	rReconcileContext.newConfirmedHumanState.fCurrentTime = rReconcileContext.fCurrentTime;

	ReconcileCatchUp(rReconcileContext, iMinConfirmedFrame);

	// Final prune
	ReconcilePruneInactiveFrames(rReconcileContext);
}

void Game::ApplyReconcileResult()
{
	// Heap: Frame deserialization, map operations
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	ReconcileContext& rReconcileContext = *mpReconcileContext;

	// Handle desync (network ops must happen on main thread)
	if (rReconcileContext.iDesyncFrame >= 0)
	{
		mpNetworkClient->SendDesyncReport(rReconcileContext.iDesyncFrame, rReconcileContext.desyncCoord, rReconcileContext.desyncServerCrc, rReconcileContext.desyncClientCrc);
		mpNetworkClient->SendDebugFrameRequest(rReconcileContext.iDesyncFrame, rReconcileContext.desyncCoord);
		mpNetworkClient->SetDesyncDebugMode(true);

		mDesyncDebugState.iFrame = rReconcileContext.iDesyncFrame;
		mDesyncDebugState.coord = rReconcileContext.desyncCoord;
		mDesyncDebugState.pClientFrame = std::move(rReconcileContext.pDesyncClientFrame);

		// Restore per-coord state (moved to context at kick time)
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			auto stateIt = mCoordReconcileStates.find(rWork.coord);
			if (stateIt == mCoordReconcileStates.end() || stateIt->second.uiGeneration != rWork.uiGeneration)
			{
				continue;
			}
			CoordReconcileState& rState = stateIt->second;

			for (auto& [iFrame, rUpdate] : rWork.serverUpdates)
			{
				if (iFrame > rState.iConfirmedFrame)
				{
					rState.serverUpdates[iFrame] = std::move(rUpdate);
				}
			}
			for (auto& [iFrame, rSnapshot] : rWork.extrapolatedSnapshots)
			{
				rState.extrapolatedSnapshots[iFrame] = std::move(rSnapshot);
			}
		}

		mpReconcileContext.reset();
		return;
	}

	// Write back per-coord results
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		auto stateIt = mCoordReconcileStates.find(rWork.coord);
		if (stateIt == mCoordReconcileStates.end() || stateIt->second.uiGeneration != rWork.uiGeneration)
		{
			continue;
		}
		CoordReconcileState& rState = stateIt->second;

		// Advance confirmed state
		if (rWork.iNewConfirmedFrame >= 0)
		{
			rState.iConfirmedFrame = rWork.iNewConfirmedFrame;
			rState.confirmedSerializedFrame = std::move(rWork.newConfirmedSerializedFrame);
		}

		// Prune stale extrapolated snapshots
		std::erase_if(rState.extrapolatedSnapshots, [&](const auto& rPair) { return rPair.first <= rState.iConfirmedFrame; });

		// Merge new extrapolated snapshots from catch-up
		for (auto& [iFrame, rSnapshot] : rWork.newExtrapolatedSnapshots)
		{
			if (iFrame > rState.iConfirmedFrame)
			{
				rState.extrapolatedSnapshots[iFrame] = std::move(rSnapshot);
			}
		}

		// Restore unconsumed server updates that were moved to context
		for (auto& [iFrame, rUpdate] : rWork.serverUpdates)
		{
			if (iFrame > rState.iConfirmedFrame)
			{
				rState.serverUpdates[iFrame] = std::move(rUpdate);
			}
		}
	}

	// Update human tracking from reconciled state
	mConfirmedHumanState = rReconcileContext.newConfirmedHumanState;
	mHumanGridCoord = rReconcileContext.humanGridCoord;
	mHumanPlayerId = rReconcileContext.humanPlayerId;
	mfPreviousHumanArmor = rReconcileContext.fPreviousHumanArmor;

	// Advance frame ID counter past worker's usage
	muiNextFrameId = std::max(muiNextFrameId, rReconcileContext.uiNextFrameId);

	if (!rReconcileContext.bCrcFastPathHandledAll)
	{
		// Use caught-up frames directly for rendering (confirmed state is for rollback only)
		mCurrentFrames = std::move(rReconcileContext.currentFrames);

		// Restore counters from caught-up state
		miFrameCounter = rReconcileContext.iFrameCounter;
		mfCurrentTime = rReconcileContext.fCurrentTime;

		// Ensure next frames exist
		EnsureNextFrames();
	}

	mpReconcileContext.reset();
}

#endif // BT_CLIENT

} // namespace game
