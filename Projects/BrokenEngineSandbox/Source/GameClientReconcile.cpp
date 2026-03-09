#include "Game.h"

#include "Profile/ProfileManager.h"

namespace game
{

#ifdef BT_CLIENT

void Game::KickReconcile()
{
	mpReconcileContext = std::make_unique<ReconcileContext>();
	ReconcileContext& rReconcileContext = *mpReconcileContext;

	// Populate per-coord work items
	for (auto& [rCoord, rSub] : mSubscribedFrames)
	{
		if (rSub.iConfirmedFrame < 0)
		{
			continue;
		}

		CoordReconcileWork work;
		work.coord = rCoord;
		work.uiGeneration = rSub.uiGeneration;
		work.iConfirmedFrame = rSub.iConfirmedFrame;
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
	using SnapshotEntry = engine::SubscribedFrame::SnapshotEntry;

	// Helper to find a snapshot index by frame number (entries are in order)
	auto findSnapshotIndex = [](std::array<SnapshotEntry, engine::SubscribedFrame::kiMaxSnapshots>& rSnapshots, int64_t iCount, int64_t iFrame) -> int64_t
	{
		for (int64_t i = 0; i < iCount; ++i)
		{
			if (rSnapshots[i].iFrame == iFrame)
			{
				return i;
			}
		}
		return -1;
	};

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
		int64_t iLastMatchedIndex = -1;
		auto it = rWork.serverUpdates.begin();

		while (it != rWork.serverUpdates.end() && it->first == iExpected && iExpected <= rReconcileContext.iTargetFrame)
		{
			int64_t iIndex = findSnapshotIndex(rWork.snapshots, rWork.iSnapshotCount, iExpected);
			if (iIndex < 0)
			{
				FILE_LOG(0, "[CrcFastPath] FAIL coord=({},{}) frame={}: snapshot missing", rWork.coord.x, rWork.coord.y, iExpected);
				bMatch = false;
				break;
			}
			if (rWork.snapshots[iIndex].crc != it->second.serverCrc)
			{
				FILE_LOG(0, "[CrcFastPath] FAIL coord=({},{}) frame={}: crc mismatch client={} server={}", rWork.coord.x, rWork.coord.y, iExpected, rWork.snapshots[iIndex].crc, it->second.serverCrc);
				bMatch = false;
				break;
			}
			if (rWork.snapshots[iIndex].inputCrc != it->second.inputCrc)
			{
				FILE_LOG(0, "[CrcFastPath] FAIL coord=({},{}) frame={}: inputCrc mismatch client={} server={}", rWork.coord.x, rWork.coord.y, iExpected, rWork.snapshots[iIndex].inputCrc, it->second.inputCrc);
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
			rReconcileContext.iCrcValidatedFrameTicks += iLastMatched - rWork.iConfirmedFrame;

			// Record matched snapshot index instead of moving Frame
			rWork.iNewConfirmedSnapshotIndex = iLastMatchedIndex;

			// Keep snapshots beyond matched frame
			rWork.newSnapshots.clear();
			for (int64_t i = 0; i < rWork.iSnapshotCount; ++i)
			{
				if (rWork.snapshots[i].iFrame > iLastMatched && rWork.snapshots[i].pFrame != nullptr)
				{
					rWork.newSnapshots.push_back(std::move(rWork.snapshots[i]));
				}
			}

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
	rReconcileContext.coordWorkIndex.clear();
	for (size_t i = 0; i < rReconcileContext.coordWork.size(); ++i)
	{
		rReconcileContext.coordWorkIndex[rReconcileContext.coordWork.at(i).coord] = i;
	}

	ReconcileRollback(rReconcileContext, iMinConfirmedFrame);

	int64_t iMaxConsecutive = ReconcileFindReplayRange(rReconcileContext, iMinConfirmedFrame);

	FILE_LOG(0, "[Reconcile] Rollback: minConfirmed={} replayRange=[{},{}] coords={} targetFrame={}", iMinConfirmedFrame, iMinConfirmedFrame + 1, iMaxConsecutive, rReconcileContext.coordWork.size(), rReconcileContext.iTargetFrame);

	ReconcileReplay(rReconcileContext, iMinConfirmedFrame, iMaxConsecutive);

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
			auto subIt = mSubscribedFrames.find(rWork.coord);
			if (subIt == mSubscribedFrames.end() || subIt->second.uiGeneration != rWork.uiGeneration)
			{
				continue;
			}
			engine::SubscribedFrame& rSub = subIt->second;

			rSub.iConfirmedSnapshotIndex = rWork.iConfirmedSnapshotIndex;

			for (auto& [iFrame, rUpdate] : rWork.serverUpdates)
			{
				if (iFrame > rSub.iConfirmedFrame)
				{
					rSub.serverUpdates[iFrame] = std::move(rUpdate);
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
		auto subIt = mSubscribedFrames.find(rWork.coord);
		if (subIt == mSubscribedFrames.end() || subIt->second.uiGeneration != rWork.uiGeneration)
		{
			continue;
		}
		engine::SubscribedFrame& rSub = subIt->second;

		// Advance confirmed state
		if (rWork.iNewConfirmedFrame >= 0)
		{
			rSub.iConfirmedFrame = rWork.iNewConfirmedFrame;

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
					if (rSub.snapshots[i].pFrame != nullptr && rSub.snapshots[i].iFrame >= rSub.iConfirmedFrame)
					{
						if (rSub.snapshots[i].iFrame == rSub.iConfirmedFrame)
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
				for (auto& rSnapshot : rWork.newSnapshots)
				{
					if (rSnapshot.iFrame > rSub.iConfirmedFrame && rSub.iSnapshotCount < engine::SubscribedFrame::kiMaxSnapshots)
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
					for (auto& rSnapshot : rWork.newSnapshots)
					{
						if (rSnapshot.pFrame != nullptr && rSnapshot.iFrame > rSub.iConfirmedFrame && rSub.iSnapshotCount < engine::SubscribedFrame::kiMaxSnapshots)
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
					for (auto& rSnapshot : rWork.newSnapshots)
					{
						if (rSnapshot.pFrame != nullptr && rSnapshot.iFrame > rSub.iConfirmedFrame && rSub.iSnapshotCount < engine::SubscribedFrame::kiMaxSnapshots)
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
		for (auto& [iFrame, rUpdate] : rWork.serverUpdates)
		{
			if (iFrame > rSub.iConfirmedFrame)
			{
				rSub.serverUpdates[iFrame] = std::move(rUpdate);
			}
		}
	}

	// Update human tracking from reconciled state
	mConfirmedHumanState = rReconcileContext.newConfirmedHumanState;
	mfPreviousHumanArmor = rReconcileContext.fPreviousHumanArmor;

	// Feed reconciliation counters to profile manager
	gpProfileManager->SetReconcileCounters(
		rReconcileContext.iCrcValidatedFrameTicks,
		rReconcileContext.iAssumedFrameTicks,
		rReconcileContext.iCrcFastPathEvents,
		rReconcileContext.iStatusChangeReplayTicks,
		rReconcileContext.iKnockOnReplayTicks);

	// Advance frame ID counter past worker's usage
	muiNextFrameId = std::max(muiNextFrameId, rReconcileContext.uiNextFrameId);

	if (!rReconcileContext.bCrcFastPathHandledAll)
	{
		// Move latest workspace frame into mSubscribedFrames for rendering.
		// Workspace entry may be null if ReconcileReplay or ReconcileCatchUp moved it to newSnapshots;
		// in that case, keep existing current (rendering uses GetSnapshotFrame during extrapolation).
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.iReplayWorkspaceUsed > 0 && rWork.replayWorkspace[rWork.iReplayWorkspaceUsed - 1] != nullptr)
			{
				std::swap(mSubscribedFrames[rWork.coord].current, rWork.replayWorkspace[rWork.iReplayWorkspaceUsed - 1]);
			}
		}

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
