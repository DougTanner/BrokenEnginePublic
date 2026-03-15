#include "Game.h"

#include "Network/ClientReconciler.h"
#include "Network/ReconcileReplay.h"
#include "Profile/ProfileManager.h"

namespace game
{

#if defined(BT_CLIENT)

ClientReconciler::ClientReconciler()
{
	if constexpr (kbEnableReconcileThread)
	{
		mpWorker = std::make_unique<common::PersistentWorker>(common::kThreadReconcile, 10 * 1'024 * 1'024);
	}
}

ClientReconciler::~ClientReconciler()
{
	if constexpr (kbEnableReconcileThread)
	{
		if (mbInFlight)
		{
			mpWorker->Wait();
			mbInFlight = false;
		}
	}
}

void ClientReconciler::TryKick()
{
	if (!mbHasNewData)
	{
		return;
	}

	Kick();
	mbInFlight = true;
	mbHasNewData = false;
}

ReconcileDesyncInfo ClientReconciler::Wait()
{
	if constexpr (kbEnableReconcileThread)
	{
		if (!mbInFlight)
		{
			return {};
		}

		common::Timer timer;
		mpWorker->Wait();
		std::chrono::nanoseconds semaphoreWaitNs = timer.GetDeltaNs(true);
		Log(kLogNetwork, "reconcileWaitMs: {}", std::chrono::duration_cast<std::chrono::milliseconds>(semaphoreWaitNs).count()); // DT: TEMP

		std::chrono::nanoseconds maxWait = std::chrono::nanoseconds(100ms);
		if (semaphoreWaitNs > maxWait)
		{
			DEBUG_BREAK();
		}

		ReconcileDesyncInfo desyncInfo = ApplyResult();
		mbInFlight = false;
		return desyncInfo;
	}
	else
	{
		return {};
	}
}

void ClientReconciler::Reset()
{
	if constexpr (kbEnableReconcileThread)
	{
		if (mbInFlight)
		{
			mpWorker->Wait();
			mbInFlight = false;
		}
		mpContext.reset();
	}

	mbHasNewData = false;
	mConfirmedHumanState = {};
}

void ClientReconciler::Kick()
{
	mpContext = std::make_unique<ReconcileContext>();
	ReconcileContext& rReconcileContext = *mpContext;

	// Populate per-coord work items
	for (auto& [rCoord, rSub] : gpGame->mCoordFrames)
	{
		if (rSub.iConfirmedTick < 0)
		{
			continue;
		}

		CoordReconcileWork work;
		work.coord = rCoord;
		work.uiGeneration = rSub.uiGeneration;
		work.iConfirmedTick = rSub.iConfirmedTick;
		work.iConfirmedOffset = rSub.iConfirmedOffset;
		work.iSnapshotHead = rSub.iSnapshotHead;
		work.serverUpdates = std::move(rSub.serverUpdates);
		work.pendingFullState = std::move(rSub.pendingFullState);

		// Swap snapshot array to worker
		std::swap(work.snapshots, rSub.snapshots);
		work.iSnapshotCount = rSub.iSnapshotCount;
		rSub.iSnapshotHead = 0;
		rSub.iSnapshotCount = 0;
		rSub.iConfirmedOffset = -1;
		rSub.serverUpdates.clear();
		rSub.pendingFullState.reset();

		rReconcileContext.coordWork.push_back(std::move(work));
	}

	// Global input
	rReconcileContext.confirmedHumanState = mConfirmedHumanState;
	rReconcileContext.uiNextFrameId = gpGame->NextFrameId();
	rReconcileContext.iTargetTick = gpGame->TickCounter();
	rReconcileContext.playerAlignment = gpGame->PlayerAlignment();

	// Dispatch to worker
	mpWorker->Wake([this]()
	{
		Reconcile(*mpContext, gpGame->Alignments());
	});
}

void ClientReconciler::Reconcile(ReconcileContext& rReconcileContext, [[maybe_unused]] const engine::Alignments& rAlignments)
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		ReconcileCoord(rReconcileContext, rWork);
		if (rReconcileContext.iDesyncTick >= 0)
		{
			return;
		}
	}

	ReconcileUpdateHumanState(rReconcileContext);
}

static void MergeNewSnapshots(engine::CoordFrames& rSub, CoordReconcileWork& rWork, int64_t iStartIndex)
{
	for (std::unique_ptr<Frame>& rSnapshot : rWork.newSnapshots)
	{
		if (rSnapshot != nullptr && rSnapshot->interpolate.iTick > rSub.iConfirmedTick && rSub.iSnapshotCount < engine::kiTickRate)
		{
			int64_t iPhysical = SnapshotIndex(iStartIndex, rSub.iSnapshotCount);
			rSub.snapshots[iPhysical] = std::move(rSnapshot);
			++rSub.iSnapshotCount;
		}
	}
}

void ClientReconciler::ApplyCoordWriteback(CoordReconcileWork& rWork, engine::CoordFrames& rSub)
{
	// Advance confirmed state
	if (rWork.iNewConfirmedTick >= 0)
	{
		rSub.iConfirmedTick = rWork.iNewConfirmedTick;

		if (rWork.bCrcFastPath)
		{
			// Save main-thread extrapolation count before swap replaces it
			int64_t iMainSnapshotCount = rSub.iSnapshotCount;

			// Fast-path: swap arrays back from worker, advance head to confirmed
			std::swap(rSub.snapshots, rWork.snapshots);
			rSub.iSnapshotHead = SnapshotIndex(rWork.iSnapshotHead, rWork.iNewConfirmedOffset);
			rSub.iSnapshotCount = rWork.iSnapshotCount - rWork.iNewConfirmedOffset;
			rSub.iConfirmedOffset = 0;

			// Merge catch-up snapshots (from non-fast-path coords that were replayed)
			MergeNewSnapshots(rSub, rWork, rSub.iSnapshotHead);

			// Preserve main-thread extrapolation snapshots built during reconciliation
			// (now in rWork.snapshots after swap, head=0 from kick reset)
			for (int64_t i = 0; i < iMainSnapshotCount && rSub.iSnapshotCount < engine::kiTickRate; ++i)
			{
				if (rWork.snapshots[i] != nullptr && rWork.snapshots[i]->interpolate.iTick > rSub.iConfirmedTick)
				{
					int64_t iPhysical = SnapshotIndex(rSub.iSnapshotHead, rSub.iSnapshotCount);
					rSub.snapshots[iPhysical] = std::move(rWork.snapshots[i]);
					++rSub.iSnapshotCount;
				}
			}
		}
		else
		{
			// Non-fast-path: worker built newSnapshots with confirmed + catch-up
			rSub.iSnapshotHead = 0;
			rSub.iSnapshotCount = 0;

			if (rWork.iNewConfirmedNewSnapshotIndex >= 0)
			{
				// Confirmed from newSnapshots (replay result)
				rSub.iConfirmedOffset = 0;
				rSub.snapshots[0] = std::move(rWork.newSnapshots[rWork.iNewConfirmedNewSnapshotIndex]);
				rSub.iSnapshotCount = 1;

				// Add remaining catch-up snapshots
				MergeNewSnapshots(rSub, rWork, 0);
			}
			else if (rWork.iNewConfirmedOffset >= 0)
			{
				// Confirmed from input snapshots (replay validated existing snapshot)
				int64_t iConfirmedPhysical = SnapshotIndex(rWork.iSnapshotHead, rWork.iNewConfirmedOffset);
				rSub.iConfirmedOffset = 0;
				rSub.snapshots[0] = std::move(rWork.snapshots[iConfirmedPhysical]);
				rSub.iSnapshotCount = 1;

				// Add catch-up snapshots
				MergeNewSnapshots(rSub, rWork, 0);
			}
		}
	}
	else
	{
		// Save main-thread extrapolation count before swap replaces it
		int64_t iMainSnapshotCount = rSub.iSnapshotCount;

		// No advancement — swap snapshots back and restore all ring fields
		std::swap(rSub.snapshots, rWork.snapshots);
		rSub.iSnapshotCount = rWork.iSnapshotCount;
		rSub.iSnapshotHead = rWork.iSnapshotHead;
		rSub.iConfirmedOffset = rWork.iConfirmedOffset;

		// Preserve main-thread extrapolation snapshots built during reconciliation
		for (int64_t i = 0; i < iMainSnapshotCount && rSub.iSnapshotCount < engine::kiTickRate; ++i)
		{
			if (rWork.snapshots[i] != nullptr && rWork.snapshots[i]->interpolate.iTick > rSub.iConfirmedTick)
			{
				int64_t iPhysical = SnapshotIndex(rSub.iSnapshotHead, rSub.iSnapshotCount);
				rSub.snapshots[iPhysical] = std::move(rWork.snapshots[i]);
				++rSub.iSnapshotCount;
			}
		}
	}

	// Restore unconsumed server updates that were moved to context
	for (auto& [iTick, rUpdate] : rWork.serverUpdates)
	{
		if (iTick > rSub.iConfirmedTick)
		{
			rSub.serverUpdates.insert_or_assign(iTick, std::move(rUpdate));
		}
	}
}

ReconcileDesyncInfo ClientReconciler::ApplyResult()
{
	// Heap: Frame deserialization, map operations
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	ReconcileContext& rReconcileContext = *mpContext;

	// Handle desync
	if (rReconcileContext.iDesyncTick >= 0)
	{
		ReconcileDesyncInfo desyncInfo;
		desyncInfo.bDesync = true;
		desyncInfo.iDesyncTick = rReconcileContext.iDesyncTick;
		desyncInfo.desyncCoord = rReconcileContext.desyncCoord;
		desyncInfo.desyncServerCrc = rReconcileContext.desyncServerCrc;
		desyncInfo.desyncClientCrc = rReconcileContext.desyncClientCrc;
		desyncInfo.pDesyncClientFrame = std::move(rReconcileContext.pDesyncClientFrame);

		// Restore per-coord state (moved to context at kick time)
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			auto subscriptionIt = gpGame->mCoordFrames.find(rWork.coord);
			if (subscriptionIt == gpGame->mCoordFrames.end() || subscriptionIt->second.uiGeneration != rWork.uiGeneration)
			{
				continue;
			}
			engine::CoordFrames& rSub = subscriptionIt->second;

			rSub.iConfirmedOffset = rWork.iConfirmedOffset;
			rSub.iSnapshotHead = rWork.iSnapshotHead;

			for (auto& [iTick, rUpdate] : rWork.serverUpdates)
			{
				if (iTick > rSub.iConfirmedTick)
				{
					rSub.serverUpdates.insert_or_assign(iTick, std::move(rUpdate));
				}
			}

			// Restore snapshots via swap back
			std::swap(rSub.snapshots, rWork.snapshots);
			rSub.iSnapshotCount = rWork.iSnapshotCount;
		}

		mpContext.reset();
		return desyncInfo;
	}

	// Write back per-coord results
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		auto subscriptionIt = gpGame->mCoordFrames.find(rWork.coord);
		if (subscriptionIt == gpGame->mCoordFrames.end() || subscriptionIt->second.uiGeneration != rWork.uiGeneration)
		{
			continue;
		}
		ApplyCoordWriteback(rWork, subscriptionIt->second);
	}

	// Update human tracking from reconciled state
	mConfirmedHumanState = rReconcileContext.newConfirmedHumanState;
	gpGame->SetPreviousHumanArmor(rReconcileContext.newConfirmedHumanState.fPreviousHumanArmor);

	// Feed reconciliation counters to profile manager
	gpProfileManager->SetReconcileCounters(rReconcileContext.profiling.iCrcValidatedFrameTicks, rReconcileContext.profiling.iAssumedFrameTicks, rReconcileContext.profiling.iCrcFastPathEvents, rReconcileContext.profiling.iStatusChangeReplayTicks, rReconcileContext.profiling.iKnockOnReplayTicks);

	// Advance frame ID counter past worker's usage
	gpGame->SetNextFrameId(std::max(gpGame->NextFrameId(), rReconcileContext.uiNextFrameId));

	if (rReconcileContext.bAnyFullReplay)
	{
		// Move latest workspace frame into mCoordFrames for rendering.
		// Workspace entry may be null if replay or catch-up moved it to newSnapshots;
		// in that case, keep existing current (rendering uses GetSnapshotFrame during extrapolation).
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.iReplayWorkspaceUsed > 0 && rWork.replayWorkspace[rWork.iReplayWorkspaceUsed - 1] != nullptr)
			{
				auto it = gpGame->mCoordFrames.find(rWork.coord);
				if (it != gpGame->mCoordFrames.end())
				{
					std::swap(it->second.pCurrent, rWork.replayWorkspace[rWork.iReplayWorkspaceUsed - 1]);
				}
			}
		}

		// Restore counters from caught-up state
		Log(kLogNetwork, "setTickCounter old: {} new: {}", gpGame->TickCounter(), rReconcileContext.iTickCounter); // DT: TEMP
		gpGame->SetTickCounter(rReconcileContext.iTickCounter);
		gpGame->SetCurrentTime(rReconcileContext.newConfirmedHumanState.fCurrentTime);

		// Ensure next frames exist
		gpGame->EnsureNextFrames();
	}

	mpContext.reset();
	return {};
}

#endif // BT_CLIENT

} // namespace game
