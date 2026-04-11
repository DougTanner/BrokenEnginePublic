#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct CoordWork;
struct ReconcileInputs;
struct ReconcileProfiling;
struct ConfirmedClientState;
struct StatusChange;

struct CrcFastPathCoordResult
{
	bool bHandled = true;
	// Set only when bHandled == false. The lowest tick > post-walk iConfirmedTick whose ring
	// frame sharedCrc did not match its server update — used by ReconcileCoord to shrink the
	// full-replay rollback window. -1 when no mismatch is pending (e.g., fast path was blocked
	// by a missing snapshot rather than a CRC failure).
	int64_t iLowestUnresolvedMismatch = -1;
};

CrcFastPathCoordResult CrcFastPathProcessCoord(CoordWork& rWork, int64_t iTargetTick);

void ReconcileCoord(CoordWork& rWork, const ReconcileInputs& rInputs);
void ReconcileUpdateClientState(std::span<const CoordWork> works, const ReconcileInputs& rInputs, bool bAnyFullReplay, ConfirmedClientState& rInOutState);
void ReconcileInjectPendingFullState(CoordWork& rWork);

} // namespace game

#endif // BT_CLIENT
