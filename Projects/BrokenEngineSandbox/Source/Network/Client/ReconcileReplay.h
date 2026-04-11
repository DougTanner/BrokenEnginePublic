#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct ReconcileContext;
struct CoordReconcileWork;
struct ReconcileProfiling;
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

CrcFastPathCoordResult CrcFastPathProcessCoord(CoordReconcileWork& rWork, int64_t iTargetTick, ReconcileProfiling& rProfiling);

void ReconcileCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork);
void ReconcileUpdateClientState(ReconcileContext& rReconcileContext);
void ReconcileInjectPendingFullState(CoordReconcileWork& rWork);

} // namespace game

#endif // BT_CLIENT
