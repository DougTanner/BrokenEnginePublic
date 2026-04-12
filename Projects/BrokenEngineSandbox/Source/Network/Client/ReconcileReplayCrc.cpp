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

static CrcValidateResult CrcValidateLoop(CoordWork& rWork, int64_t iTargetTick)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;

	CrcValidateResult result;
	int64_t iLowestUnresolved = std::numeric_limits<int64_t>::max();

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
				LOG(kNetwork, kVerbose, "CrcValidateLoop Earlier mismatch resolved by later match Coord: ({},{}) MismatchTick: {} MatchTick: {}", rWork.coord.x, rWork.coord.y, iLowestUnresolved, iTick);
				iLowestUnresolved = std::numeric_limits<int64_t>::max();
			}
		}
		else
		{
			char acSharedCrc[20] {}, acClientCrc[20] {};
			common::ToHex(std::span<char, 20>(acSharedCrc), it->second.sharedCrc);
			common::ToHex(std::span<char, 20>(acClientCrc), rClientFrame.postRender.sharedCrc);
			LOG(kNetwork, kVerbose, "CrcValidateLoop sharedCrc mismatch Coord: ({},{}) Tick: {} ServerCrc: {} ClientCrc: {} StatusChanges: {}", rWork.coord.x, rWork.coord.y, iTick, acSharedCrc, acClientCrc, it->second.statusChanges.size());
			for (const StatusChange& rStatusChange : it->second.statusChanges)
			{
				LOG(kNetwork, kVerbose, "  StatusChange type: {}", StatusChangeTypeName(rStatusChange.eType));
			}
			if (iLowestUnresolved == std::numeric_limits<int64_t>::max())
			{
				iLowestUnresolved = iTick;
			}
		}
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

	rScratch.bCrcFastPath = true;
	rScratch.iNewConfirmedTick = iHighestMatch;
	rScratch.profiling.iCrcValidatedFrameTicks += iHighestMatch - rFrames.iConfirmedTick;

	rScratch.iNewConfirmedOffset = SnapshotIndex(rFrames.iSnapshotHead, iHighestMatchIndex);
	rScratch.iOutputCount = rFrames.iSnapshotCount - iHighestMatchIndex;

	// Drop validated entries — fast-path advances iConfirmedTick in place, so anything
	// at or below it is now consumed and would otherwise accumulate in serverUpdates.
	rFrames.serverUpdates.erase(rFrames.serverUpdates.begin(), rFrames.serverUpdates.upper_bound(iHighestMatch));
}

CrcFastPathCoordResult CrcFastPathProcessCoord(CoordWork& rWork, int64_t iTargetTick)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;

	CrcFastPathCoordResult result;

	if (rFrames.serverUpdates.empty() && !rFrames.pendingFullState.has_value())
	{
		return result;
	}

	if (rFrames.pendingFullState.has_value())
	{
		result.bHandled = false;
		LOG(kNetwork, kVerbose, "CrcFastPathProcessCoord Pending full state forces reconcile Coord: ({},{})", rWork.coord.x, rWork.coord.y);
		return result;
	}

	CrcValidateResult validateResult = CrcValidateLoop(rWork, iTargetTick);

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
		rFrames.iHighWaterValidatedTick = std::max(rFrames.iHighWaterValidatedTick, validateResult.iHighestMatch);

		// Advance iConfirmedTick/iConfirmedOffset so any subsequent rollback starts at the new
		// confirmed point. Required by the Part 1 invariant (no re-simulation of validated ticks).
		rFrames.iConfirmedTick = validateResult.iHighestMatch;
		rFrames.iConfirmedOffset = validateResult.iHighestMatchIndex;

		if (!validateResult.bMatch)
		{
			result.bHandled = false;
			result.iLowestUnresolvedMismatch = validateResult.iLowestUnresolvedMismatch;
			LOG(kNetwork, kVerbose, "CrcFastPathProcessCoord Matched with unresolved mismatch Coord: ({},{}) HighestMatch: {} LowestMismatch: {}", rWork.coord.x, rWork.coord.y, validateResult.iHighestMatch, validateResult.iLowestUnresolvedMismatch);
		}
	}
	else if (!validateResult.bMatch)
	{
		result.bHandled = false;
		result.iLowestUnresolvedMismatch = validateResult.iLowestUnresolvedMismatch;
		LOG(kNetwork, kVerbose, "CrcFastPathProcessCoord No snapshot match, deferring to replay Coord: ({},{}) FirstMismatchTick: {}", rWork.coord.x, rWork.coord.y, validateResult.iLowestUnresolvedMismatch);
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
