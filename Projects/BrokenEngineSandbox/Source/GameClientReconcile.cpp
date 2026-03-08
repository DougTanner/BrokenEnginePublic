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

	FILE_LOG(0, "[KickReconcile] coords={} targetFrame={}", rReconcileContext.coordWork.size(), rReconcileContext.iTargetFrame);

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
	int64_t iNewMinConfirmed = std::numeric_limits<int64_t>::max();

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
			if (rWork.iConfirmedFrame >= 0 && rWork.iConfirmedFrame < iNewMinConfirmed)
			{
				iNewMinConfirmed = rWork.iConfirmedFrame;
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

		while (it != rWork.serverUpdates.end() && it->first == iExpected && iExpected <= rReconcileContext.iTargetFrame)
		{
			auto snapIt = rWork.extrapolatedSnapshots.find(iExpected);
			if (snapIt == rWork.extrapolatedSnapshots.end())
			{
				FILE_LOG(0, "[CrcFastPath] FAIL coord=({},{}) frame={}: snapshot missing", rWork.coord.x, rWork.coord.y, iExpected);
				bMatch = false;
				break;
			}
			if (snapIt->second.crc != it->second.serverCrc)
			{
				FILE_LOG(0, "[CrcFastPath] FAIL coord=({},{}) frame={}: crc mismatch client={} server={}", rWork.coord.x, rWork.coord.y, iExpected, snapIt->second.crc, it->second.serverCrc);
				bMatch = false;
				break;
			}
			if (snapIt->second.inputCrc != it->second.inputCrc)
			{
				FILE_LOG(0, "[CrcFastPath] FAIL coord=({},{}) frame={}: inputCrc mismatch client={} server={}", rWork.coord.x, rWork.coord.y, iExpected, snapIt->second.inputCrc, it->second.inputCrc);
				bMatch = false;
				break;
			}
			iLastMatched = iExpected;
			++iExpected;
			++it;
		}

		// Gap at confirmed+1 for this coord: first server update is non-consecutive.
		// Keep the updates (gap may be filled by resend), but treat as handled
		// so this coord doesn't force the expensive full reconcile path.
		if (iLastMatched == -1 && !rWork.serverUpdates.empty() && rWork.serverUpdates.begin()->first != rWork.iConfirmedFrame + 1)
		{
			FILE_LOG(0, "[CrcFastPath] GAP coord=({},{}) confirmed={} firstUpdate={}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedFrame, rWork.serverUpdates.begin()->first);
			if (rWork.iConfirmedFrame >= 0 && rWork.iConfirmedFrame < iMinConfirmedFrame)
			{
				iMinConfirmedFrame = rWork.iConfirmedFrame;
			}
			if (rWork.iConfirmedFrame >= 0 && rWork.iConfirmedFrame < iNewMinConfirmed)
			{
				iNewMinConfirmed = rWork.iConfirmedFrame;
			}
			continue;
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
			if (iLastMatched < iNewMinConfirmed)
			{
				iNewMinConfirmed = iLastMatched;
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
		if (iOldMinConfirmed != std::numeric_limits<int64_t>::max() && iNewMinConfirmed > iOldMinConfirmed)
		{
			for (int64_t i = 0; i < iNewMinConfirmed - iOldMinConfirmed; ++i)
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
	common::Timer reconcileTimer;

	auto [bAllHandled, iMinConfirmedFrame] = ReconcileCrcFastPath(rReconcileContext);
	if (bAllHandled)
	{
		FILE_LOG(0, "[Reconcile] CRC fast-path handled all coords={}", rReconcileContext.coordWork.size());
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

	FILE_LOG(0, "[Reconcile] Rollback: minConfirmed={} replayRange=[{},{}] coords={} targetFrame={}", iMinConfirmedFrame, iMinConfirmedFrame + 1, iMaxConsecutive, rReconcileContext.coordWork.size(), rReconcileContext.iTargetFrame);

	ReconcileReplay(rReconcileContext, iMinConfirmedFrame, iMaxConsecutive, coordWorkIndex);

	FILE_LOG(0, "[Reconcile] Replay complete: frameCounter={} desync={}", rReconcileContext.iFrameCounter, rReconcileContext.iDesyncFrame >= 0);

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

	FILE_LOG(0, "[Reconcile] CatchUp: from={} to={}", rReconcileContext.iFrameCounter, rReconcileContext.iTargetFrame);

	ReconcileCatchUp(rReconcileContext, iMinConfirmedFrame);

	// Final prune
	ReconcilePruneInactiveFrames(rReconcileContext);

	FILE_LOG(0, "[Reconcile] Total: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(reconcileTimer.GetDeltaNs()).count());
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
