#pragma once

#if defined(BT_CLIENT)

#include "Network/Client/ClientReconciler.h"

namespace game
{

struct CoordWork;
struct ReconcileInputs;
struct ConfirmedClientState;
struct StatusChange;

struct CrcFastPathCoordResult
{
	bool bHandled = true;
	RingLayout preWritebackLayout;
	// Set only when bHandled == false. The lowest tick > post-walk iConfirmedTick whose ring
	// frame sharedCrc did not match its server update — used by ReconcileCoord to shrink the
	// full-replay rollback window. -1 when no mismatch is pending (e.g., fast path was blocked
	// by a missing snapshot rather than a CRC failure).
	int64_t iLowestUnresolvedMismatch = -1;
};

inline bool HasDuePendingFullState(const engine::CoordFrames& rFrames, int64_t iTargetTick)
{
	return rFrames.pendingFullState.has_value() && rFrames.pendingFullState->iTick <= iTargetTick;
}

CrcFastPathCoordResult CrcFastPathProcessCoord(CoordWork& rWork, int64_t iTargetTick);

void ReconcileCoord(CoordWork& rWork, const ReconcileInputs& rInputs);
void ReconcileUpdateClientState(std::span<const CoordWork> works, bool bAnyFullReplay, ConfirmedClientState& rInOutState);
void ReconcileInjectPendingFullState(CoordWork& rWork);

void ReconcileRollbackCoord(CoordWork& rWork, int64_t iRollbackOffset);
int64_t ReconcileFindReplayRangeCoord(CoordWork& rWork, int64_t iReplayStart);
void ReconcileReplayCoord(CoordWork& rWork, int64_t iReplayStart, int64_t iRollbackOffset, int64_t iMaxConsecutive, float& rfTime);
void ReconcileCatchUpCoord(CoordWork& rWork, int64_t iTargetTick, float& rfTime);
void ReconcileFastPathCatchUp(CoordWork& rWork, int64_t iTargetTick);

} // namespace game

#endif // BT_CLIENT
