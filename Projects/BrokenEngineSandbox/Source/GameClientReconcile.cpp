#include "Game.h"

#include "Profile/ProfileManager.h"

namespace game
{

#if defined(BT_CLIENT)

void Game::KickReconcile()
{
	mpReconcileContext = std::make_unique<ReconcileContext>();
	ReconcileContext& rReconcileContext = *mpReconcileContext;

	// Populate per-coord work items
	for (auto& [rCoord, rSub] : mCoordFrames)
	{
		if (rSub.iConfirmedTick < 0)
		{
			continue;
		}

		CoordReconcileWork work;
		work.coord = rCoord;
		work.uiGeneration = rSub.uiGeneration;
		work.iConfirmedTick = rSub.iConfirmedTick;
		work.iConfirmedSnapshotIndex = rSub.iConfirmedSnapshotIndex;
		work.serverUpdates = std::move(rSub.serverUpdates);
		work.pendingFullState = std::move(rSub.pendingFullState);

		// Swap snapshot array to worker
		work.snapshots.swap(rSub.snapshots);
		work.iSnapshotCount = rSub.iSnapshotCount;
		rSub.iSnapshotCount = 0;
		rSub.iConfirmedSnapshotIndex = -1;
		rSub.serverUpdates.clear();
		rSub.pendingFullState.reset();

		rReconcileContext.coordWork.push_back(std::move(work));
	}

	// Global input
	rReconcileContext.confirmedHumanState = mConfirmedHumanState;
	rReconcileContext.uiNextFrameId = muiNextFrameId;
	rReconcileContext.iTargetTick = miTickCounter;
	rReconcileContext.playerAlignment = mPlayerAlignment;

	// Dispatch to worker
	mpReconcileWorker->Wake([this]()
	{
		Reconcile(*mpReconcileContext, mAlignments);
	});
}

std::pair<bool, int64_t> Game::ReconcileCrcFastPath(ReconcileContext& rReconcileContext)
{
	// Helper to find a snapshot index by frame number (entries are in order)
	auto findSnapshotIndex = [](std::array<std::unique_ptr<Frame>, engine::kiTickRate>& rSnapshots, int64_t iCount, int64_t iTick) -> int64_t
	{
		for (int64_t i = 0; i < iCount; ++i)
		{
			if (rSnapshots[i] != nullptr && rSnapshots[i]->interpolate.iTick == iTick)
			{
				return i;
			}
		}
		return -1;
	};

	bool bAllHandled = true;
	int64_t iMinConfirmedTick = std::numeric_limits<int64_t>::max();
	int64_t iNewMinConfirmed = std::numeric_limits<int64_t>::max();

	int64_t iOldMinConfirmed = std::numeric_limits<int64_t>::max();
	for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iOldMinConfirmed)
		{
			iOldMinConfirmed = rWork.iConfirmedTick;
		}
	}

	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.serverUpdates.empty() && !rWork.pendingFullState.has_value())
		{
			if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iMinConfirmedTick)
			{
				iMinConfirmedTick = rWork.iConfirmedTick;
			}
			if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iNewMinConfirmed)
			{
				iNewMinConfirmed = rWork.iConfirmedTick;
			}
			continue;
		}

		if (rWork.pendingFullState.has_value())
		{
			bAllHandled = false;
			if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iMinConfirmedTick)
			{
				iMinConfirmedTick = rWork.iConfirmedTick;
			}
			continue;
		}

		// Check CRC fast-path for this coord
		int64_t iExpected = rWork.iConfirmedTick + 1;
		bool bMatch = true;
		int64_t iLastMatched = -1;
		int64_t iLastMatchedIndex = -1;
		auto it = rWork.serverUpdates.begin();

		while (it != rWork.serverUpdates.end() && it->first == iExpected && iExpected <= rReconcileContext.iTargetTick)
		{
			int64_t iIndex = findSnapshotIndex(rWork.snapshots, rWork.iSnapshotCount, iExpected);
			if (iIndex < 0)
			{
				FILE_LOG(0, "[CrcFastPath] BREAK coord=({},{}) frame={}: snapshot missing", rWork.coord.x, rWork.coord.y, iExpected);
				break;
			}
			if (rWork.snapshots[iIndex]->postRender.serverCrc != it->second.serverCrc)
			{
				FILE_LOG(0, "[CrcFastPath] FAIL coord=({},{}) frame={}: crc mismatch client={} server={}", rWork.coord.x, rWork.coord.y, iExpected, rWork.snapshots[iIndex]->postRender.serverCrc, it->second.serverCrc);
				bMatch = false;
				break;
			}
			if (rWork.snapshots[iIndex]->postRender.previousInputCrc != it->second.inputCrc)
			{
				FILE_LOG(0, "[CrcFastPath] FAIL coord=({},{}) frame={}: inputCrc mismatch client={} server={}", rWork.coord.x, rWork.coord.y, iExpected, rWork.snapshots[iIndex]->postRender.previousInputCrc, it->second.inputCrc);
				bMatch = false;
				break;
			}
			iLastMatched = iExpected;
			iLastMatchedIndex = iIndex;
			++iExpected;
			++it;
		}

		// Gap at confirmed+1 for this coord: first server update is non-consecutive.
		// Keep the updates (gap may be filled by resend), but treat as handled
		// so this coord doesn't force the expensive full reconcile path.
		if (iLastMatched == -1 && !rWork.serverUpdates.empty() && rWork.serverUpdates.begin()->first != rWork.iConfirmedTick + 1)
		{
			FILE_LOG(0, "[CrcFastPath] GAP coord=({},{}) confirmed={} firstUpdate={}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick, rWork.serverUpdates.begin()->first);
			if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iMinConfirmedTick)
			{
				iMinConfirmedTick = rWork.iConfirmedTick;
			}
			if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iNewMinConfirmed)
			{
				iNewMinConfirmed = rWork.iConfirmedTick;
			}
			continue;
		}

		// No snapshots to validate: confirmed tick is at or past target tick.
		// Keep updates for next reconciliation when snapshots are available.
		if (iLastMatched == -1 && bMatch && rWork.iConfirmedTick >= rReconcileContext.iTargetTick)
		{
			FILE_LOG(0, "[CrcFastPath] DEFERRED coord=({},{}) confirmed={} targetTick={}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick, rReconcileContext.iTargetTick);
			if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iMinConfirmedTick)
			{
				iMinConfirmedTick = rWork.iConfirmedTick;
			}
			if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iNewMinConfirmed)
			{
				iNewMinConfirmed = rWork.iConfirmedTick;
			}
			continue;
		}

		if (iLastMatched >= 0)
		{
			rWork.bCrcFastPath = true;
			rWork.iNewConfirmedTick = iLastMatched;
			rReconcileContext.iCrcValidatedFrameTicks += iLastMatched - rWork.iConfirmedTick;

			// Record matched snapshot index instead of moving Frame
			rWork.iNewConfirmedSnapshotIndex = iLastMatchedIndex;

			// Keep snapshots beyond matched frame
			rWork.newSnapshots.clear();
			for (int64_t i = 0; i < rWork.iSnapshotCount; ++i)
			{
				if (rWork.snapshots[i] != nullptr && rWork.snapshots[i]->interpolate.iTick > iLastMatched)
				{
					rWork.newSnapshots.push_back(std::move(rWork.snapshots[i]));
				}
			}

			if (rWork.iConfirmedTick < iMinConfirmedTick)
			{
				iMinConfirmedTick = rWork.iConfirmedTick;
			}
			if (iLastMatched < iNewMinConfirmed)
			{
				iNewMinConfirmed = iLastMatched;
			}
		}
		else if (!bMatch)
		{
			// CRC mismatch at confirmed+1: permanent (StatusChanges the client didn't have).
			// Must reconcile to replay with server data.
			FILE_LOG(0, "[CrcFastPath] MISMATCH coord=({},{}) confirmed={}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick);
			bAllHandled = false;
			if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iMinConfirmedTick)
			{
				iMinConfirmedTick = rWork.iConfirmedTick;
			}
		}
		else
		{
			// Snapshot missing at confirmed+1.
			// If confirmed+1 < targetTick, the snapshot should have existed but was lost
			// (physics recorded it but the reconcile swap-back overwrote it). Treat as
			// mismatch to trigger full reconcile which will regenerate snapshots.
			// If confirmed+1 >= targetTick, physics genuinely hasn't reached that tick yet — defer.
			if (rWork.iConfirmedTick + 1 < rReconcileContext.iTargetTick)
			{
				FILE_LOG(0, "[CrcFastPath] STALE-NOSNAPSHOT coord=({},{}) confirmed={} targetTick={}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick, rReconcileContext.iTargetTick);
				bAllHandled = false;
				if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iMinConfirmedTick)
				{
					iMinConfirmedTick = rWork.iConfirmedTick;
				}
			}
			else
			{
				FILE_LOG(0, "[CrcFastPath] DEFERRED-NOSNAPSHOT coord=({},{}) confirmed={}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick);
				if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iMinConfirmedTick)
				{
					iMinConfirmedTick = rWork.iConfirmedTick;
				}
				if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iNewMinConfirmed)
				{
					iNewMinConfirmed = rWork.iConfirmedTick;
				}
				continue;
			}
		}
	}

	if (bAllHandled)
	{
		rReconcileContext.bCrcFastPathHandledAll = true;
		rReconcileContext.iCrcFastPathEvents = 1;
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

	return {bAllHandled, iMinConfirmedTick};
}

void Game::Reconcile(ReconcileContext& rReconcileContext, [[maybe_unused]] const engine::Alignments& rAlignments)
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	common::Timer reconcileTimer;

	auto [bAllHandled, iMinConfirmedTick] = ReconcileCrcFastPath(rReconcileContext);
	if (bAllHandled)
	{
		return;
	}

	// Build coord-to-index map for O(1) lookups
	rReconcileContext.coordWorkIndex.clear();
	for (size_t i = 0; i < rReconcileContext.coordWork.size(); ++i)
	{
		rReconcileContext.coordWorkIndex[rReconcileContext.coordWork.at(i).coord] = i;
	}

	ReconcileRollback(rReconcileContext, iMinConfirmedTick);

	int64_t iMaxConsecutive = ReconcileFindReplayRange(rReconcileContext, iMinConfirmedTick);

	FILE_LOG(0, "[Reconcile] Rollback: minConfirmed={} replayRange=[{},{}] coords={} targetFrame={}", iMinConfirmedTick, iMinConfirmedTick + 1, iMaxConsecutive, rReconcileContext.coordWork.size(), rReconcileContext.iTargetTick);

	ReconcileReplay(rReconcileContext, iMinConfirmedTick, iMaxConsecutive);

	FILE_LOG(0, "[Reconcile] Replay complete: frameCounter={} desync={}", rReconcileContext.iTickCounter, rReconcileContext.iDesyncTick >= 0);

	if (rReconcileContext.iDesyncTick >= 0)
	{
		return;
	}

	ReconcilePruneInactiveFrames(rReconcileContext);

	// Save confirmed state
	rReconcileContext.newConfirmedHumanState.humanGridCoord = rReconcileContext.humanGridCoord;
	rReconcileContext.newConfirmedHumanState.humanPlayerId = rReconcileContext.humanPlayerId;
	rReconcileContext.newConfirmedHumanState.fPreviousHumanArmor = rReconcileContext.fPreviousHumanArmor;
	rReconcileContext.newConfirmedHumanState.fCurrentTime = rReconcileContext.fCurrentTime;

	FILE_LOG(0, "[Reconcile] CatchUp: from={} to={}", rReconcileContext.iTickCounter, rReconcileContext.iTargetTick);

	ReconcileCatchUp(rReconcileContext, iMinConfirmedTick);

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
	if (rReconcileContext.iDesyncTick >= 0)
	{
		mpNetworkClient->SendDesyncReport(rReconcileContext.iDesyncTick, rReconcileContext.desyncCoord, rReconcileContext.desyncServerCrc, rReconcileContext.desyncClientCrc);
		mpNetworkClient->SendDebugFrameRequest(rReconcileContext.iDesyncTick, rReconcileContext.desyncCoord);
		mpNetworkClient->SetDesyncDebugMode(true);

		mDesyncDebugState.iTick = rReconcileContext.iDesyncTick;
		mDesyncDebugState.coord = rReconcileContext.desyncCoord;
		mDesyncDebugState.pClientFrame = std::move(rReconcileContext.pDesyncClientFrame);

		// Restore per-coord state (moved to context at kick time)
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			auto subscriptionIt = mCoordFrames.find(rWork.coord);
			if (subscriptionIt == mCoordFrames.end() || subscriptionIt->second.uiGeneration != rWork.uiGeneration)
			{
				continue;
			}
			engine::CoordFrames& rSub = subscriptionIt->second;

			rSub.iConfirmedSnapshotIndex = rWork.iConfirmedSnapshotIndex;

			for (auto& [iTick, rUpdate] : rWork.serverUpdates)
			{
				if (iTick > rSub.iConfirmedTick)
				{
					rSub.serverUpdates[iTick] = std::move(rUpdate);
				}
			}

			// Restore snapshots via swap back
			rSub.snapshots.swap(rWork.snapshots);
			rSub.iSnapshotCount = rWork.iSnapshotCount;
		}

		mpReconcileContext.reset();
		return;
	}

	// Write back per-coord results
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		auto subscriptionIt = mCoordFrames.find(rWork.coord);
		if (subscriptionIt == mCoordFrames.end() || subscriptionIt->second.uiGeneration != rWork.uiGeneration)
		{
			continue;
		}
		engine::CoordFrames& rSub = subscriptionIt->second;

		// Advance confirmed state
		if (rWork.iNewConfirmedTick >= 0)
		{
			rSub.iConfirmedTick = rWork.iNewConfirmedTick;

			if (rWork.bCrcFastPath)
			{
				// Fast-path: confirmed is in the worker's input snapshots
				rSub.iConfirmedSnapshotIndex = rWork.iNewConfirmedSnapshotIndex;
				// Swap arrays back from worker
				rSub.snapshots.swap(rWork.snapshots);
				// Compact: keep confirmed + entries after confirmed frame
				int64_t iWriteIndex = 0;
				for (int64_t i = 0; i < rWork.iSnapshotCount; ++i)
				{
					if (rSub.snapshots[i] != nullptr && rSub.snapshots[i]->interpolate.iTick >= rSub.iConfirmedTick)
					{
						if (rSub.snapshots[i]->interpolate.iTick == rSub.iConfirmedTick)
						{
							rSub.iConfirmedSnapshotIndex = iWriteIndex;
						}
						if (iWriteIndex != i)
						{
							rSub.snapshots[iWriteIndex] = std::move(rSub.snapshots[i]);
						}
						++iWriteIndex;
					}
				}
				rSub.iSnapshotCount = iWriteIndex;

				// Merge catch-up snapshots (from non-fast-path coords that were replayed)
				for (std::unique_ptr<Frame>& rSnapshot : rWork.newSnapshots)
				{
					if (rSnapshot != nullptr && rSnapshot->interpolate.iTick > rSub.iConfirmedTick && rSub.iSnapshotCount < engine::kiTickRate)
					{
						rSub.snapshots[rSub.iSnapshotCount++] = std::move(rSnapshot);
					}
				}

				// Append main thread's extrapolation entries created since kick
				// (main thread's snapshot array was zeroed at kick, so nothing to merge)
			}
			else
			{
				// Non-fast-path: worker built newSnapshots with confirmed + catch-up
				rSub.iSnapshotCount = 0;

				if (rWork.iNewConfirmedNewSnapshotIndex >= 0)
				{
					// Confirmed from newSnapshots (replay result)
					rSub.iConfirmedSnapshotIndex = 0;
					rSub.snapshots[0] = std::move(rWork.newSnapshots[rWork.iNewConfirmedNewSnapshotIndex]);
					rSub.iSnapshotCount = 1;

					// Add remaining catch-up snapshots
					for (std::unique_ptr<Frame>& rSnapshot : rWork.newSnapshots)
					{
						if (rSnapshot != nullptr && rSnapshot->interpolate.iTick > rSub.iConfirmedTick && rSub.iSnapshotCount < engine::kiTickRate)
						{
							rSub.snapshots[rSub.iSnapshotCount++] = std::move(rSnapshot);
						}
					}
				}
				else if (rWork.iNewConfirmedSnapshotIndex >= 0)
				{
					// Confirmed from input snapshots (replay validated existing snapshot)
					rSub.iConfirmedSnapshotIndex = 0;
					rSub.snapshots[0] = std::move(rWork.snapshots[rWork.iNewConfirmedSnapshotIndex]);
					rSub.iSnapshotCount = 1;

					// Add catch-up snapshots
					for (std::unique_ptr<Frame>& rSnapshot : rWork.newSnapshots)
					{
						if (rSnapshot != nullptr && rSnapshot->interpolate.iTick > rSub.iConfirmedTick && rSub.iSnapshotCount < engine::kiTickRate)
						{
							rSub.snapshots[rSub.iSnapshotCount++] = std::move(rSnapshot);
						}
					}
				}
			}
		}
		else
		{
			// No advancement — swap snapshots back
			rSub.snapshots.swap(rWork.snapshots);
			rSub.iSnapshotCount = rWork.iSnapshotCount;
			rSub.iConfirmedSnapshotIndex = rWork.iConfirmedSnapshotIndex;
		}

		// Restore unconsumed server updates that were moved to context
		for (auto& [iTick, rUpdate] : rWork.serverUpdates)
		{
			if (iTick > rSub.iConfirmedTick)
			{
				rSub.serverUpdates[iTick] = std::move(rUpdate);
			}
		}
	}

	// Update human tracking from reconciled state
	mConfirmedHumanState = rReconcileContext.newConfirmedHumanState;
	mfPreviousHumanArmor = rReconcileContext.fPreviousHumanArmor;

	// Feed reconciliation counters to profile manager
	gpProfileManager->SetReconcileCounters(rReconcileContext.iCrcValidatedFrameTicks, rReconcileContext.iAssumedFrameTicks, rReconcileContext.iCrcFastPathEvents, rReconcileContext.iStatusChangeReplayTicks, rReconcileContext.iKnockOnReplayTicks);

	// Advance frame ID counter past worker's usage
	muiNextFrameId = std::max(muiNextFrameId, rReconcileContext.uiNextFrameId);

	if (!rReconcileContext.bCrcFastPathHandledAll)
	{
		// Move latest workspace frame into mCoordFrames for rendering.
		// Workspace entry may be null if ReconcileReplay or ReconcileCatchUp moved it to newSnapshots;
		// in that case, keep existing current (rendering uses GetSnapshotFrame during extrapolation).
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.iReplayWorkspaceUsed > 0 && rWork.replayWorkspace[rWork.iReplayWorkspaceUsed - 1] != nullptr)
			{
				std::swap(mCoordFrames[rWork.coord].pCurrent, rWork.replayWorkspace[rWork.iReplayWorkspaceUsed - 1]);
			}
		}

		// Restore counters from caught-up state
		miTickCounter = rReconcileContext.iTickCounter;
		mfCurrentTime = rReconcileContext.fCurrentTime;

		// Ensure next frames exist
		EnsureNextFrames();
	}

	mpReconcileContext.reset();
}

#endif // BT_CLIENT

} // namespace game
