#include "Network/Client/ReconcileReplay.h"

#include "Game.h"

#include "Network/Client/ClientReconciler.h"

namespace game
{

#if defined(BT_CLIENT)

static int64_t FindSnapshotIndex(std::unique_ptr<Frame> (&rSnapshots)[engine::kiNetworkBufferSize], int64_t iHead, int64_t iCount, int64_t iTick)
{
	for (int64_t i = 0; i < iCount; ++i)
	{
		int64_t iPhysical = SnapshotIndex(iHead, i);
		if (rSnapshots[iPhysical] != nullptr && rSnapshots[iPhysical]->interpolate.iTick == iTick)
		{
			return i;
		}
	}
	return -1;
}

struct CrcValidateResult
{
	// True when no unresolved mismatches remain past iHighestMatch.
	bool bMatch = true;
	// Highest tick whose ring-frame sharedCrc matched its server update. -1 if no match.
	int64_t iHighestMatch = -1;
	int64_t iHighestMatchIndex = -1;
	// Lowest tick > iConfirmedTick whose ring-frame sharedCrc did NOT match. -1 if none.
	// Mismatches at ticks < iHighestMatch are bypassed (they'll be dropped anyway).
	int64_t iLowestUnresolvedMismatch = -1;
};

static CrcValidateResult CrcValidateLoop(CoordWork& rWork, int64_t iTargetTick, bool bSuppressRepeatLogs)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;

	CrcValidateResult result;
	int64_t iLowestUnresolved = std::numeric_limits<int64_t>::max();
	int64_t iMismatchCount = 0;
	int64_t iAdditionalMismatchesWithStatusChanges = 0;
	int64_t iSecondMismatchTick = -1;
	int64_t iLastMismatchTick = -1;

	// Walk in tick-ascending order. iHighestMatch grows monotonically, so whenever it advances
	// any prior-tracked mismatch becomes bypassed (older than the new confirmed tick) and we
	// can reset the unresolved tracker.
	for (auto it = rFrames.serverUpdates.lower_bound(rFrames.iConfirmedTick + 1);
		it != rFrames.serverUpdates.end() && it->first <= iTargetTick;
		++it)
	{
		int64_t iTick = it->first;
		int64_t iIndex = FindSnapshotIndex(rFrames.snapshots, rFrames.iSnapshotHead, rFrames.iSnapshotCount, iTick);
		if (iIndex < 0)
		{
			continue;
		}
		int64_t iPhysical = SnapshotIndex(rFrames.iSnapshotHead, iIndex);
		const Frame& rClientFrame = *rFrames.snapshots[iPhysical];

		if (rClientFrame.postRender.sharedCrc == it->second.sharedCrc)
		{
			result.iHighestMatch = iTick;
			result.iHighestMatchIndex = iIndex;
			// Any prior-tracked mismatch is now bypassed — the new confirmed point is past it.
			if (iLowestUnresolved != std::numeric_limits<int64_t>::max())
			{
				LOG(kNetwork, kDebug, "CrcValidateLoop Speculative CRC mismatch superseded by later CRC match Coord: ({},{}) MismatchTick: {} MatchTick: {}", rWork.coord.x, rWork.coord.y, iLowestUnresolved, iTick);
				iLowestUnresolved = std::numeric_limits<int64_t>::max();
			}
		}
		else
		{
			if (!bSuppressRepeatLogs && iMismatchCount == 0)
			{
				char acSharedCrc[20] {}, acClientCrc[20] {};
				common::ToHex(std::span<char, 20>(acSharedCrc), it->second.sharedCrc);
				common::ToHex(std::span<char, 20>(acClientCrc), rClientFrame.postRender.sharedCrc);

				// Collapse StatusChange types into counted format
				common::ScopedWorkbufferArena builder = common::gpThreadLocal->mWorkbuffer.Push();
				if (!it->second.statusChanges.empty())
				{
					int64_t counts[static_cast<int64_t>(StatusChangeType::kCount)] {};
					for (const StatusChange& rStatusChange : it->second.statusChanges)
					{
						++counts[static_cast<int64_t>(rStatusChange.eType)];
					}
					bool bFirst = true;
					for (int64_t i = 0; i < static_cast<int64_t>(StatusChangeType::kCount); ++i)
					{
						if (counts[i] > 0)
						{
							if (!bFirst)
							{
								builder.Append(", ");
							}
							builder.Append(StatusChangeTypeName(static_cast<StatusChangeType>(i)));
							if (counts[i] > 1)
							{
								builder.Append("x");
								builder.Append(counts[i]);
							}
							bFirst = false;
						}
					}
				}

				LOG(kNetwork, kDebug, "CrcValidateLoop Speculative CRC mismatch; reconciliation pending Coord: ({},{}) ForTick: {} ServerCrc: {} ClientCrc: {} StatusChanges: {} [{}] TicksSinceFullState: {}", rWork.coord.x, rWork.coord.y, iTick, acSharedCrc, acClientCrc, it->second.statusChanges.size(), builder.View(), (rFrames.iLastFullStateTick >= 0) ? (iTick - rFrames.iLastFullStateTick) : int64_t{-1});
			}
			++iMismatchCount;
			if (iMismatchCount > 1)
			{
				if (iSecondMismatchTick < 0)
				{
					iSecondMismatchTick = iTick;
				}
				if (!it->second.statusChanges.empty())
				{
					++iAdditionalMismatchesWithStatusChanges;
				}
			}
			// NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) — read only by the kVerbose summary LOG below, which compiles out at default log levels
			iLastMismatchTick = iTick;
			if (iLowestUnresolved == std::numeric_limits<int64_t>::max())
			{
				iLowestUnresolved = iTick;
			}
		}
	}

	if (!bSuppressRepeatLogs && iMismatchCount > 1)
	{
		LOG(kNetwork, kVerbose, "CrcValidateLoop {} additional mismatches Coord: ({},{}) Ticks: {}-{} ({} with StatusChanges)", iMismatchCount - 1, rWork.coord.x, rWork.coord.y, iSecondMismatchTick, iLastMismatchTick, iAdditionalMismatchesWithStatusChanges);
	}

	if (iLowestUnresolved != std::numeric_limits<int64_t>::max())
	{
		result.iLowestUnresolvedMismatch = iLowestUnresolved;
		result.bMatch = false;
	}

	return result;
}

static void CrcApplyMatchResult(CoordWork& rWork, int64_t iHighestMatch, int64_t iHighestMatchIndex)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	rScratch.flags.Set(ReconcileScratchFlags::kCrcFastPath);
	rScratch.iNewConfirmedTick = iHighestMatch;
	rScratch.profiling.iCrcValidatedFrameTicks += iHighestMatch - rFrames.iConfirmedTick;

	// Retain kiRenderBehindTicks frames before the confirmed match so the renderer always has
	// a prev-tail (or N prev-tails) available for interpolation.
	rScratch.outputLayout = ComputeRetention(rFrames.iSnapshotHead, rFrames.iSnapshotCount, iHighestMatchIndex);

	// Drop validated entries — fast-path advances iConfirmedTick in place, so anything
	// at or below it is now consumed and would otherwise accumulate in serverUpdates.
	rFrames.serverUpdates.erase(rFrames.serverUpdates.begin(), rFrames.serverUpdates.upper_bound(iHighestMatch));
}

CrcFastPathCoordResult CrcFastPathProcessCoord(CoordWork& rWork, int64_t iTargetTick)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;

	CrcFastPathCoordResult result {
		.preWritebackLayout = {
			.iHead = rFrames.iSnapshotHead,
			.iCount = rFrames.iSnapshotCount,
			.iConfirmedInner = rFrames.iConfirmedOffset,
		},
	};

	if (rFrames.serverUpdates.empty() && !HasDuePendingFullState(rFrames, iTargetTick))
	{
		return result;
	}

	if (HasDuePendingFullState(rFrames, iTargetTick))
	{
		result.bHandled = false;
		LOG(kNetwork, kVerbose, "CrcFastPathProcessCoord Due pending full state forces reconcile Coord: ({},{})", rWork.coord.x, rWork.coord.y);
		return result;
	}

	// Compute log suppression: if confirmed tick and first mismatch tick are unchanged from
	// last frame, this is a repeat stuck state — suppress per-tick mismatch detail logging.
	bool bSameState = (rFrames.iConfirmedTick == rFrames.iLastLoggedConfirmedTick);
	if (bSameState && !rFrames.serverUpdates.empty())
	{
		auto itFirst = rFrames.serverUpdates.upper_bound(rFrames.iConfirmedTick);
		bSameState = (itFirst != rFrames.serverUpdates.end() && itFirst->first == rFrames.iLastLoggedFirstMismatch);
	}

	// Cooldown: suppress detail logging when mismatch was recently logged (covers multiple
	// Run() calls at the same or adjacent ticks within a single render frame)
	bool bCooldownActive = (rFrames.iLastMismatchDetailLogTick >= 0 &&
		iTargetTick - rFrames.iLastMismatchDetailLogTick < engine::CoordFrames::kiMismatchDetailLogCooldown);

	CrcValidateResult validateResult = CrcValidateLoop(rWork, iTargetTick, bSameState || bCooldownActive);

	// Update dedup state after validation
	if (bSameState)
	{
		++rFrames.iStuckFrameCount;
		rWork.scratch.flags.Set(ReconcileScratchFlags::kSuppressRepeatLogs);
		if ((rFrames.iStuckFrameCount % engine::CoordFrames::kiStuckLogInterval) == 0)
		{
			LOG(kNetwork, kVerbose, "CrcValidateLoop still stuck Coord: ({},{}) ConfirmedTick: {} FirstMismatch: {} StuckFrames: {}", rWork.coord.x, rWork.coord.y, rFrames.iConfirmedTick, rFrames.iLastLoggedFirstMismatch, rFrames.iStuckFrameCount);
		}
	}
	else if (!validateResult.bMatch)
	{
		rFrames.iLastLoggedConfirmedTick = rFrames.iConfirmedTick;
		rFrames.iLastLoggedFirstMismatch = validateResult.iLowestUnresolvedMismatch;
		rFrames.iStuckFrameCount = 0;
		if (!bCooldownActive)
		{
			rFrames.iLastMismatchDetailLogTick = iTargetTick;
		}
		else
		{
			rWork.scratch.flags.Set(ReconcileScratchFlags::kSuppressRepeatLogs);
		}
	}

	// Gap at confirmed+1 with no matches/mismatches: first server update is non-consecutive
	// and nothing was validatable. Nothing for the fast path or full replay to do this cycle.
	if (validateResult.iHighestMatch == -1 && validateResult.bMatch && !rFrames.serverUpdates.empty() && rFrames.serverUpdates.begin()->first != rFrames.iConfirmedTick + 1)
	{
		return result;
	}

	// No matches, no mismatches, already at/past target.
	if (validateResult.iHighestMatch == -1 && validateResult.bMatch && rFrames.iConfirmedTick >= iTargetTick)
	{
		return result;
	}

	if (validateResult.iHighestMatch >= 0)
	{
		CrcApplyMatchResult(rWork, validateResult.iHighestMatch, validateResult.iHighestMatchIndex);
		result.preWritebackLayout.iConfirmedInner = validateResult.iHighestMatchIndex;
		rFrames.iHighWaterValidatedTick = std::max(rFrames.iHighWaterValidatedTick, validateResult.iHighestMatch);

		// Advance iConfirmedTick/iConfirmedOffset so any subsequent rollback starts at the new
		// confirmed point. Required by the Part 1 invariant (no re-simulation of validated ticks).
		rFrames.iConfirmedTick = validateResult.iHighestMatch;
		rFrames.iConfirmedOffset = validateResult.iHighestMatchIndex;

		if (!validateResult.bMatch)
		{
			result.bHandled = false;
			result.iLowestUnresolvedMismatch = validateResult.iLowestUnresolvedMismatch;
			if (!(rWork.scratch.flags & ReconcileScratchFlags::kSuppressRepeatLogs))
			{
				LOG(kNetwork, kDebug, "CrcFastPathProcessCoord Later CRC match left an unresolved speculative mismatch; rollback/replay required Coord: ({},{}) HighestMatch: {} LowestMismatch: {}", rWork.coord.x, rWork.coord.y, validateResult.iHighestMatch, validateResult.iLowestUnresolvedMismatch);
			}
		}
	}
	else if (!validateResult.bMatch)
	{
		result.bHandled = false;
		result.iLowestUnresolvedMismatch = validateResult.iLowestUnresolvedMismatch;
		if (!(rWork.scratch.flags & ReconcileScratchFlags::kSuppressRepeatLogs))
		{
			LOG(kNetwork, kDebug, "CrcFastPathProcessCoord No snapshot CRC match; rollback/replay required Coord: ({},{}) FirstMismatchTick: {}", rWork.coord.x, rWork.coord.y, validateResult.iLowestUnresolvedMismatch);
		}
	}
	else if (rFrames.iConfirmedTick + 1 < iTargetTick)
	{
		// No matches, no mismatches, but target is past confirmed — gaps in serverUpdates.
		// Fall through to full replay to extend the ring (or run catch-up).
		result.bHandled = false;
	}

	return result;
}

#endif // BT_CLIENT

} // namespace game
