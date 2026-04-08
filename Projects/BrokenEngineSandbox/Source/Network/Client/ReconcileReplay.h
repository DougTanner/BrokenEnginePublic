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
};

CrcFastPathCoordResult CrcFastPathProcessCoord(CoordReconcileWork& rWork, int64_t iTargetTick, ReconcileProfiling& rProfiling);

void ReconcileCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork);
void ReconcileUpdateClientState(ReconcileContext& rReconcileContext);
void ReconcileInjectPendingFullState(CoordReconcileWork& rWork);

} // namespace game

#endif // BT_CLIENT
