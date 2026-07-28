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

	if (!rFrames.pendingFullState)
	{
		return;
	}
	auto& rPending = *rFrames.pendingFullState;
	ASSERT(rPending.pFrame->interpolate.iTick == rPending.iTick);
	int64_t iSlot = SnapshotIndex(rScratch.iReplayWriteHead, rScratch.iReplayWriteCount);
	rPending.pFrame->postRender.sharedCrc = rPending.pFrame->Crcs();
	rFrames.snapshots[iSlot] = std::move(rPending.pFrame);
	rScratch.replayStack.clear();
	rScratch.replayStack.push_back(rFrames.snapshots[iSlot].get());
	rScratch.iReplayStackCount = 1;
	rScratch.iReplayWriteHead = SnapshotIndex(iSlot, 1);
	rScratch.iReplayWriteCount = 0;
	// Full state replaces the timeline; a prior higher high-water mark was against a discarded timeline.
	rFrames.iHighWaterValidatedTick = rPending.iTick;
	rFrames.iLastFullStateTick = rPending.iTick;
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
		rFrames.iSnapshotHead = rScratch.outputLayout.iHead;
		rFrames.iConfirmedOffset = rScratch.outputLayout.iConfirmedInner;
		rFrames.iSnapshotCount = rScratch.outputLayout.iCount;
		ASSERT(rFrames.iSnapshotCount >= 0 && rFrames.iSnapshotCount <= engine::kiNetworkBufferSize);
	}
}

static void AdoptUnreachablePendingFullState(CoordWork& rWork)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;
	engine::CoordFrames::PendingFullState& rPending = *rFrames.pendingFullState;

	ASSERT(rPending.pFrame != nullptr);
	ASSERT(rPending.pFrame->interpolate.iTick == rPending.iTick);

	const int64_t iAdoptedTick = rPending.iTick;
	const int64_t iAdoptedSlot = SnapshotIndex(rScratch.iReplayWriteHead, rScratch.iReplayWriteCount);
	rPending.pFrame->postRender.sharedCrc = rPending.pFrame->Crcs();
	rFrames.snapshots[iAdoptedSlot] = std::move(rPending.pFrame);

	rScratch.replayStack.clear();
	rScratch.replayStack.push_back(rFrames.snapshots[iAdoptedSlot].get());
	rScratch.iReplayStackCount = 1;
	rScratch.iReplayWriteHead = SnapshotIndex(iAdoptedSlot, 1);
	rScratch.iReplayWriteCount = 0;
	rScratch.iLastValidatedIndex = -1;
	rScratch.iNewConfirmedTick = iAdoptedTick;
	rScratch.outputLayout = {
		.iHead = iAdoptedSlot,
		.iCount = 1,
		.iConfirmedInner = 0,
	};

	rFrames.iHighWaterValidatedTick = iAdoptedTick;
	rFrames.iLastFullStateTick = iAdoptedTick;
	rFrames.pendingFullState.reset();
	rFrames.serverUpdates.erase(rFrames.serverUpdates.begin(), rFrames.serverUpdates.upper_bound(iAdoptedTick));
	rFrames.iLastReplayConfirmedTick = -1;
	rFrames.iLastReplayServerUpdateCount = -1;

	LOG(kNetwork, kWarning, "ReconcileCoord Adopted authoritative full state past update gap Coord: ({},{}) AdoptedTick: {}", rWork.coord.x, rWork.coord.y, iAdoptedTick);
}

// Aggressive CRC walk: finds the highest matching ring frame across all server updates
// in range, advances iConfirmedTick/iConfirmedOffset to it, and reports the lowest
// unresolved mismatch (if any) past the new confirmed point. Returns true if the fast
// path fully resolved this coord (writeback + catch-up already done, caller should
// return); false if the caller should continue to full replay using rOutResult.
static bool ApplyCrcFastPath(CoordWork& rWork, const ReconcileInputs& rInputs, CrcFastPathCoordResult& rOutResult)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	rOutResult = CrcFastPathProcessCoord(rWork, rInputs.iTargetTick);
	if (rOutResult.bHandled)
	{
		++rScratch.profiling.iCrcFastPathEvents;
		rFrames.iLastReplayConfirmedTick = -1;
		rFrames.iLastReplayServerUpdateCount = -1;
		ApplyCoordWriteback(rWork);
		ReconcileFastPathCatchUp(rWork, rInputs.iTargetTick);
		return true;
	}

	// The walk may have set the output layout via CrcApplyMatchResult —
	// preserve those as the floor result. If full replay validates further, ReconcileValidateCrcCoord
	// and ReconcileReplayCoord will overwrite them. Count must be recomputed from scratch
	// because walk's count included old speculative frames that replay will overwrite.
	rScratch.flags.Clear(ReconcileScratchFlags::kCrcFastPath);
	rScratch.outputLayout.iCount = 0;
	return false;
}

// No server data at the first tick past confirmed — replay cannot start. Keep existing
// speculative ring (populated by prior catch-up) and wait for resend. Returns true if
// this short-circuit applied (caller should return from ReconcileCoord).
static bool EarlyReturnIfNoServerData(CoordWork& rWork, const ReconcileInputs& rInputs, const CrcFastPathCoordResult& rFastPathResult)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	if (rFrames.serverUpdates.contains(rFrames.iConfirmedTick + 1) || HasDuePendingFullState(rFrames, rInputs.iTargetTick))
	{
		return false;
	}

	if (rScratch.iNewConfirmedTick >= 0)
	{
		rScratch.outputLayout = ComputeRetention(rFastPathResult.preWritebackLayout.iHead, rFastPathResult.preWritebackLayout.iCount, rFastPathResult.preWritebackLayout.iConfirmedInner);
		ApplyCoordWriteback(rWork);
	}
	ReconcileFastPathCatchUp(rWork, rInputs.iTargetTick);
	return true;
}

// Determine rollback base: prefer the shrunk target (one tick before the lowest unresolved
// mismatch), using the speculative ring frame at that logical offset as the starting state.
// Fall back to iConfirmedTick if the shrunk target is unavailable or the walk didn't find
// a mismatch (e.g., due pending full state or gap-only path). Only attempt shrunk rollback
// if the base frame was CRC-validated — speculative frames from catch-up are guaranteed
// wrong when a gap caused the mismatch.
static void DetermineRollbackBase(CoordWork& rWork, const ReconcileInputs& rInputs, const CrcFastPathCoordResult& rFastPathResult, int64_t& iRollbackTick, int64_t& iRollbackOffset, bool& bShrunkRollback)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	iRollbackTick = rFrames.iConfirmedTick;
	iRollbackOffset = rFastPathResult.preWritebackLayout.iConfirmedInner;
	bShrunkRollback = false;

	if (rFastPathResult.iLowestUnresolvedMismatch > rFrames.iConfirmedTick + 1 && !HasDuePendingFullState(rFrames, rInputs.iTargetTick))
	{
		int64_t iShrunkTick = rFastPathResult.iLowestUnresolvedMismatch - 1;
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
}

// Roll back to the selected base, inject a reachable pending full state, and replay the consecutive
// range. The fallback retries from the typed full-confirmed layout after a provisional shrunk-base
// desync; only its empty range resolves the coord here.
enum class ReplayMode
{
	kPrimary,
	kFallback,
};

static bool RunReplay(CoordWork& rWork, const ReconcileInputs& rInputs, const RingLayout& rPreWritebackLayout, int64_t& iRollbackTick, int64_t& iRollbackOffset, bool& bShrunkRollback, ReplayMode eMode, float& fTime, int64_t& iReplayStart)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	if (eMode == ReplayMode::kFallback)
	{
		if (!(rScratch.flags & ReconcileScratchFlags::kSuppressRepeatLogs))
		{
			LOG(kNetwork, kDebug, "ReconcileCoord Provisional shrunk-rollback mismatch; retrying from full rollback Coord: ({},{}) DesyncTick: {}", rWork.coord.x, rWork.coord.y, rScratch.iDesyncTick);
		}
		rScratch.iDesyncTick = -1;
		rScratch.desyncExpectedCrc = 0;
		rScratch.desyncActualCrc = 0;
		rScratch.pDesyncClientFrame.reset();
		rScratch.iLastValidatedIndex = -1;

		iRollbackTick = rFrames.iConfirmedTick;
		iRollbackOffset = rPreWritebackLayout.iConfirmedInner;
		bShrunkRollback = false;
		rScratch.flags.Clear(ReconcileScratchFlags::kShrunkRollback);
	}

	ReconcileRollbackCoord(rWork, iRollbackOffset);
	fTime = rScratch.replayStack[0]->interpolate.fCurrentTime;

	// Inject a due pending full state at the confirmed frame. Future states remain queued.
	if (HasDuePendingFullState(rFrames, rInputs.iTargetTick) && rFrames.pendingFullState->iTick == rFrames.iConfirmedTick)
	{
		ReconcileInjectPendingFullState(rWork);
		fTime = rScratch.replayStack[0]->interpolate.fCurrentTime;
		if (eMode == ReplayMode::kPrimary)
		{
			LOG(kNetwork, kVerbose, "ReconcileCoord Injected pending full state Coord: ({},{}) AtTick: {}", rWork.coord.x, rWork.coord.y, rFrames.iConfirmedTick);
		}
	}
	else if (eMode == ReplayMode::kPrimary && HasDuePendingFullState(rFrames, rInputs.iTargetTick) && rFrames.pendingFullState->iTick < rFrames.iConfirmedTick)
	{
		LOG(kNetwork, kVerbose, "ReconcileCoord Discarded stale pending full state Coord: ({},{}) FullStateTick: {} ConfirmedTick: {}", rWork.coord.x, rWork.coord.y, rFrames.pendingFullState->iTick, rFrames.iConfirmedTick);
		rFrames.pendingFullState.reset();
	}

	iReplayStart = iRollbackTick + 1;
	const int64_t iUncappedMaxConsecutive = ReconcileFindReplayRangeCoord(rWork, iReplayStart);
	if (eMode == ReplayMode::kPrimary && HasDuePendingFullState(rFrames, rInputs.iTargetTick) && rFrames.pendingFullState->iTick > iUncappedMaxConsecutive)
	{
		const int64_t iAdoptedTick = rFrames.pendingFullState->iTick;
		AdoptUnreachablePendingFullState(rWork);
		fTime = rScratch.replayStack[0]->interpolate.fCurrentTime;
		iReplayStart = iAdoptedTick + 1;
		return false;
	}
	const int64_t iMaxConsecutive = std::min(iUncappedMaxConsecutive, rInputs.iTargetTick);

	if (eMode == ReplayMode::kFallback && iMaxConsecutive < iReplayStart && !HasDuePendingFullState(rFrames, rInputs.iTargetTick))
	{
		if (rScratch.iNewConfirmedTick >= 0)
		{
			rScratch.outputLayout = ComputeRetention(rPreWritebackLayout.iHead, rPreWritebackLayout.iCount, rPreWritebackLayout.iConfirmedInner);
			ApplyCoordWriteback(rWork);
		}
		ReconcileFastPathCatchUp(rWork, rInputs.iTargetTick);
		return true;
	}

	ReconcileReplayCoord(rWork, iReplayStart, iRollbackOffset, iMaxConsecutive, fTime);
	return false;
}

// Compute output layout: confirmed frame + remaining replay/catch-up frames. Four subcases
// fold together: validation past the base, validation at the base, walk advanced confirmed
// but replay didn't validate further, and catch-up after a gap.
static void ComputeOutputLayout(CoordWork& rWork, int64_t iRollbackOffset)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	if (rScratch.iLastValidatedIndex > 0)
	{
		rScratch.outputLayout.iCount = rScratch.iReplayWriteCount - (rScratch.iLastValidatedIndex - 1);
	}
	else if (rScratch.iLastValidatedIndex == 0)
	{
		rScratch.outputLayout.iCount = rScratch.iReplayWriteCount + 1;
	}
	else if (rScratch.iNewConfirmedTick >= 0)
	{
		// Walk advanced iConfirmedTick but full replay didn't validate anything further.
		// Preserve walk's confirmed frame as the base and include new catch-up frames.
		rScratch.outputLayout.iCount = rScratch.iReplayWriteCount + 1;
	}
	else
	{
		// Full replay ran catch-up without validating (gap in serverUpdates past confirmed).
		// Preserve existing confirmed tick/offset as the base so catch-up frames are committed.
		rScratch.iNewConfirmedTick = rFrames.iConfirmedTick;
		rScratch.outputLayout = {
			.iHead = SnapshotIndex(rFrames.iSnapshotHead, iRollbackOffset),
			.iCount = rScratch.iReplayWriteCount + 1,
			.iConfirmedInner = 0,
		};
	}

	rScratch.outputLayout.iCount = std::min(rScratch.outputLayout.iCount, static_cast<int64_t>(engine::kiNetworkBufferSize));
	ASSERT(rScratch.outputLayout.iCount >= 0 && rScratch.outputLayout.iCount <= engine::kiNetworkBufferSize);
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

	CrcFastPathCoordResult fastPathResult {};
	if (ApplyCrcFastPath(rWork, rInputs, fastPathResult))
	{
		return;
	}

	if (EarlyReturnIfNoServerData(rWork, rInputs, fastPathResult))
	{
		return;
	}

	rScratch.flags.Set(ReconcileScratchFlags::kReplayed);

	// Invariant: full replay must not repeat identical work. If iConfirmedTick and serverUpdates
	// are unchanged since the last full replay attempt, the result would be the same.
	int64_t iCurrentUpdateCount = static_cast<int64_t>(rFrames.serverUpdates.size());
	if (rFrames.iConfirmedTick == rFrames.iLastReplayConfirmedTick
		&& iCurrentUpdateCount == rFrames.iLastReplayServerUpdateCount
		&& !HasDuePendingFullState(rFrames, rInputs.iTargetTick))
	{
		DEBUG_BREAK();
	}
	rFrames.iLastReplayConfirmedTick = rFrames.iConfirmedTick;
	rFrames.iLastReplayServerUpdateCount = iCurrentUpdateCount;

	int64_t iRollbackTick = 0;
	int64_t iRollbackOffset = 0;
	bool bShrunkRollback = false;
	DetermineRollbackBase(rWork, rInputs, fastPathResult, iRollbackTick, iRollbackOffset, bShrunkRollback);

	float fTime = 0.0f;
	int64_t iReplayStart = 0;
	RunReplay(rWork, rInputs, fastPathResult.preWritebackLayout, iRollbackTick, iRollbackOffset, bShrunkRollback, ReplayMode::kPrimary, fTime, iReplayStart);

	if (rScratch.iDesyncTick >= 0 && bShrunkRollback && rScratch.iDesyncTick == iReplayStart)
	{
		if (RunReplay(rWork, rInputs, fastPathResult.preWritebackLayout, iRollbackTick, iRollbackOffset, bShrunkRollback, ReplayMode::kFallback, fTime, iReplayStart))
		{
			return;
		}
	}

	if (rScratch.iDesyncTick >= 0)
	{
		return;
	}

	ReconcileCatchUpCoord(rWork, rInputs.iTargetTick, fTime);

	ComputeOutputLayout(rWork, iRollbackOffset);

	ASSERT(rScratch.replayStack[rScratch.iReplayStackCount - 1]->interpolate.iTick <= rInputs.iTargetTick);

	ApplyCoordWriteback(rWork);
}

#endif // BT_CLIENT

} // namespace game
