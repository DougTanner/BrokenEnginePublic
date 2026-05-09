#include "Network/Client/ReconcileReplay.h"

#include "Game.h"
#include "Network/Client/ClientReconciler.h"

namespace game
{

#if defined(BT_CLIENT)

void ReconcileInjectPendingFullState(CoordWork& rWork)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	ASSERT(rFrames.pendingFullState->pFrame->interpolate.iTick == rFrames.pendingFullState->iTick);
	int64_t iSlot = SnapshotIndex(rScratch.iReplayWriteHead, rScratch.iReplayWriteCount);
	rFrames.snapshots[iSlot] = std::move(rFrames.pendingFullState->pFrame);
	rScratch.replayStack.clear();
	rScratch.replayStack.push_back(rFrames.snapshots[iSlot].get());
	rScratch.iReplayStackCount = 1;
	rScratch.iReplayWriteHead = SnapshotIndex(iSlot, 1);
	rScratch.iReplayWriteCount = 0;
	// Full state replaces the timeline; a prior higher high-water mark was against a discarded timeline.
	rFrames.iHighWaterValidatedTick = rFrames.pendingFullState->iTick;
	rFrames.iLastFullStateTick = rFrames.pendingFullState->iTick;
	rFrames.pendingFullState.reset();
}

// Apply scratch output to rFrames in-place — writeback runs inside the same dispatch worker
// since the scratch already mutated rFrames fields (iHighWaterValidatedTick, iConfirmedTick via
// fast path, serverUpdates erase, snapshot slot allocation). This routine commits the final
// ring layout (iConfirmedTick/iSnapshotHead/iConfirmedOffset/iSnapshotCount) for success paths.
static void ApplyCoordWriteback(CoordWork& rWork)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	if (rScratch.iNewConfirmedTick >= 0)
	{
		rFrames.iConfirmedTick = rScratch.iNewConfirmedTick;
		rFrames.iSnapshotHead = rScratch.iNewConfirmedOffset;
		// iNewConfirmedInnerOffset is nonzero only when the fast-path retained frames before
		// confirmed for render-behind; replay/rollback paths leave it 0 (head == confirmed).
		rFrames.iConfirmedOffset = rScratch.iNewConfirmedInnerOffset;
		rFrames.iSnapshotCount = rScratch.iOutputCount;
		ASSERT(rFrames.iSnapshotCount >= 0 && rFrames.iSnapshotCount <= engine::kiNetworkBufferSize);
	}
}

void ReconcileCoord(CoordWork& rWork, const ReconcileInputs& rInputs)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	// Capture pre-reconcile ring tail tick so ReconcileReplayCoord can distinguish actual
	// re-simulation (replay of a tick that already existed) from first-time forward sim.
	if (rFrames.iSnapshotCount > 0)
	{
		int64_t iTailPhysical = SnapshotIndex(rFrames.iSnapshotHead, rFrames.iSnapshotCount - 1);
		if (rFrames.snapshots[iTailPhysical] != nullptr)
		{
			rScratch.iPreReconcileTailTick = rFrames.snapshots[iTailPhysical]->interpolate.iTick;
		}
	}

	// Aggressive CRC walk: finds the highest matching ring frame across all server updates
	// in range, advances iConfirmedTick/iConfirmedOffset to it, and reports the lowest
	// unresolved mismatch (if any) past the new confirmed point.
	CrcFastPathCoordResult fastPathResult = CrcFastPathProcessCoord(rWork, rInputs.iTargetTick);
	if (fastPathResult.bHandled)
	{
		++rScratch.profiling.iCrcFastPathEvents;
		rFrames.iLastReplayConfirmedTick = -1;
		rFrames.iLastReplayServerUpdateCount = -1;
		ApplyCoordWriteback(rWork);
		ReconcileFastPathCatchUp(rWork, rInputs.iTargetTick);
		return;
	}

	// The walk may have set iNewConfirmedTick/iNewConfirmedOffset via CrcApplyMatchResult —
	// preserve those as the floor result. If full replay validates further, ReconcileValidateCrcCoord
	// and ReconcileReplayCoord will overwrite them. iOutputCount must be recomputed from scratch
	// because walk's count included old speculative frames that replay will overwrite.
	rScratch.flags.Clear(ReconcileScratchFlags::kCrcFastPath);
	rScratch.iOutputCount = 0;

	// No server data at the first tick past confirmed — replay cannot start.
	// Keep existing speculative ring (populated by prior catch-up) and wait for resend.
	if (!rFrames.serverUpdates.contains(rFrames.iConfirmedTick + 1) && !rFrames.pendingFullState.has_value())
	{
		if (rScratch.iNewConfirmedTick >= 0)
		{
			// rFrames.iConfirmedOffset was mutated by CrcValidateCoord to iHighestMatchIndex
			// (offset from OLD head to confirmed). Retention shifts the new head back by
			// kiRenderBehindTicks slots so the renderer retains a prev-tail — iOutputCount
			// must count from the retained head, not from confirmed.
			int64_t iHeadAdvance = std::max<int64_t>(0, rFrames.iConfirmedOffset - engine::kiRenderBehindTicks);
			rScratch.iOutputCount = rFrames.iSnapshotCount - iHeadAdvance;
			ApplyCoordWriteback(rWork);
		}
		ReconcileFastPathCatchUp(rWork, rInputs.iTargetTick);
		return;
	}

	rScratch.flags.Set(ReconcileScratchFlags::kReplayed);

	// Invariant: full replay must not repeat identical work. If iConfirmedTick and serverUpdates
	// are unchanged since the last full replay attempt, the result would be the same.
	int64_t iCurrentUpdateCount = static_cast<int64_t>(rFrames.serverUpdates.size());
	if (rFrames.iConfirmedTick == rFrames.iLastReplayConfirmedTick
		&& iCurrentUpdateCount == rFrames.iLastReplayServerUpdateCount
		&& !rFrames.pendingFullState.has_value())
	{
		DEBUG_BREAK();
	}
	rFrames.iLastReplayConfirmedTick = rFrames.iConfirmedTick;
	rFrames.iLastReplayServerUpdateCount = iCurrentUpdateCount;

	// Determine rollback base: prefer the shrunk target (one tick before the lowest unresolved
	// mismatch), using the speculative ring frame at that logical offset as the starting state.
	// Fall back to iConfirmedTick if the shrunk target is unavailable or the walk didn't find
	// a mismatch (e.g., pending full state or gap-only path).
	// Only attempt shrunk rollback if the base frame was CRC-validated — speculative frames
	// from catch-up are guaranteed wrong when a gap caused the mismatch.
	int64_t iRollbackTick = rFrames.iConfirmedTick;
	int64_t iRollbackOffset = rFrames.iConfirmedOffset;
	bool bShrunkRollback = false;
	if (fastPathResult.iLowestUnresolvedMismatch > rFrames.iConfirmedTick + 1 && !rFrames.pendingFullState.has_value())
	{
		int64_t iShrunkTick = fastPathResult.iLowestUnresolvedMismatch - 1;
		if (iShrunkTick <= rFrames.iHighWaterValidatedTick)
		{
			int64_t iShrunkIndex = -1;
			for (int64_t i = 0; i < rFrames.iSnapshotCount; ++i)
			{
				int64_t iPhysical = SnapshotIndex(rFrames.iSnapshotHead, i);
				if (rFrames.snapshots[iPhysical] != nullptr && rFrames.snapshots[iPhysical]->interpolate.iTick == iShrunkTick)
				{
					iShrunkIndex = i;
					break;
				}
			}
			if (iShrunkIndex >= 0)
			{
				iRollbackTick = iShrunkTick;
				iRollbackOffset = iShrunkIndex;
				bShrunkRollback = true;
				rScratch.flags.Set(ReconcileScratchFlags::kShrunkRollback);
			}
		}
	}

	ReconcileRollbackCoord(rWork, iRollbackOffset);
	float fTime = rScratch.replayStack[0]->interpolate.fCurrentTime;

	// Inject pending full state at confirmed frame (stale states rejected at receive time).
	// Only applies to the full-rollback path — shrunk rollback disables this branch above.
	if (rFrames.pendingFullState.has_value() && rFrames.pendingFullState->iTick == rFrames.iConfirmedTick)
	{
		ReconcileInjectPendingFullState(rWork);
		fTime = rScratch.replayStack[0]->interpolate.fCurrentTime;
		LOG(kNetwork, kVerbose, "ReconcileCoord Injected pending full state Coord: ({},{}) AtTick: {}", rWork.coord.x, rWork.coord.y, rFrames.iConfirmedTick);
	}
	else if (rFrames.pendingFullState.has_value() && rFrames.pendingFullState->iTick < rFrames.iConfirmedTick)
	{
		LOG(kNetwork, kVerbose, "ReconcileCoord Discarded stale pending full state Coord: ({},{}) FullStateTick: {} ConfirmedTick: {}", rWork.coord.x, rWork.coord.y, rFrames.pendingFullState->iTick, rFrames.iConfirmedTick);
		rFrames.pendingFullState.reset();
	}

	int64_t iReplayStart = iRollbackTick + 1;
	int64_t iMaxConsecutive = std::min(ReconcileFindReplayRangeCoord(rWork, iReplayStart), rInputs.iTargetTick);

	ReconcileReplayCoord(rWork, rInputs, iReplayStart, iRollbackOffset, iMaxConsecutive, fTime);

	// Two-tier rollback fallback: if shrunk rollback desynced at the very first replay tick,
	// the speculative starting state was bad. Clear the desync, reset replay state, and retry
	// with a full rollback to iConfirmedTick.
	if (rScratch.iDesyncTick >= 0 && bShrunkRollback && rScratch.iDesyncTick == iReplayStart)
	{
		if (!(rScratch.flags & ReconcileScratchFlags::kSuppressRepeatLogs))
		{
			LOG(kNetwork, kError, "ReconcileCoord Shrunk rollback failed Coord: ({},{}) DesyncTick: {} — falling back to full rollback", rWork.coord.x, rWork.coord.y, rScratch.iDesyncTick);
		}
		rScratch.iDesyncTick = -1;
		rScratch.desyncExpectedCrc = 0;
		rScratch.desyncActualCrc = 0;
		rScratch.pDesyncClientFrame.reset();
		rScratch.iLastValidatedIndex = -1;

		iRollbackTick = rFrames.iConfirmedTick;
		iRollbackOffset = rFrames.iConfirmedOffset;
		bShrunkRollback = false;
		rScratch.flags.Clear(ReconcileScratchFlags::kShrunkRollback);

		ReconcileRollbackCoord(rWork, iRollbackOffset);
		fTime = rScratch.replayStack[0]->interpolate.fCurrentTime;

		if (rFrames.pendingFullState.has_value() && rFrames.pendingFullState->iTick == rFrames.iConfirmedTick)
		{
			ReconcileInjectPendingFullState(rWork);
			fTime = rScratch.replayStack[0]->interpolate.fCurrentTime;
		}

		iReplayStart = iRollbackTick + 1;
		iMaxConsecutive = std::min(ReconcileFindReplayRangeCoord(rWork, iReplayStart), rInputs.iTargetTick);

		// Safety net: if full rollback range is also empty, skip replay + catch-up entirely.
		if (iMaxConsecutive < iReplayStart && !rFrames.pendingFullState.has_value())
		{
			if (rScratch.iNewConfirmedTick >= 0)
			{
				// See the matching block above: retention shifts the head back by
				// kiRenderBehindTicks slots. iOutputCount must count from retained head.
				int64_t iHeadAdvance = std::max<int64_t>(0, rFrames.iConfirmedOffset - engine::kiRenderBehindTicks);
				rScratch.iOutputCount = rFrames.iSnapshotCount - iHeadAdvance;
				ApplyCoordWriteback(rWork);
			}
			ReconcileFastPathCatchUp(rWork, rInputs.iTargetTick);
			return;
		}

		ReconcileReplayCoord(rWork, rInputs, iReplayStart, iRollbackOffset, iMaxConsecutive, fTime);
	}

	if (rScratch.iDesyncTick >= 0)
	{
		return;
	}

	ReconcileCatchUpCoord(rWork, rInputs.iTargetTick, fTime);

	// Compute output layout: confirmed frame + remaining replay/catch-up frames
	if (rScratch.iLastValidatedIndex > 0)
	{
		rScratch.iOutputCount = rScratch.iReplayWriteCount - (rScratch.iLastValidatedIndex - 1);
	}
	else if (rScratch.iLastValidatedIndex == 0)
	{
		rScratch.iOutputCount = rScratch.iReplayWriteCount + 1;
	}
	else if (rScratch.iNewConfirmedTick >= 0)
	{
		// Walk advanced iConfirmedTick but full replay didn't validate anything further.
		// Preserve walk's confirmed frame as the base and include new catch-up frames.
		rScratch.iOutputCount = rScratch.iReplayWriteCount + 1;
	}
	else
	{
		// Full replay ran catch-up without validating (gap in serverUpdates past confirmed).
		// Preserve existing confirmed tick/offset as the base so catch-up frames are committed.
		rScratch.iNewConfirmedTick = rFrames.iConfirmedTick;
		rScratch.iNewConfirmedOffset = SnapshotIndex(rFrames.iSnapshotHead, iRollbackOffset);
		rScratch.iOutputCount = rScratch.iReplayWriteCount + 1;
	}

	rScratch.iOutputCount = std::min(rScratch.iOutputCount, static_cast<int64_t>(engine::kiNetworkBufferSize));
	ASSERT(rScratch.iOutputCount >= 0 && rScratch.iOutputCount <= engine::kiNetworkBufferSize);

	ASSERT(rScratch.replayStack[rScratch.iReplayStackCount - 1]->interpolate.iTick <= rInputs.iTargetTick);

	ApplyCoordWriteback(rWork);
}

#endif // BT_CLIENT

} // namespace game
