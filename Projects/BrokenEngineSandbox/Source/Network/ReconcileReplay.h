#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct ReconcileContext;
struct CoordReconcileWork;

std::pair<bool, int64_t> ReconcileCrcFastPath(ReconcileContext& rReconcileContext);
void ReconcileRunTick(ReconcileContext& rReconcileContext);
void ReconcileComputeActiveCoords(ReconcileContext& rReconcileContext);
void ReconcileBuildFrameInput(ReconcileContext& rReconcileContext, int64_t iServerTick, const std::unordered_map<engine::GridCoord, engine::CoordFrames::CoordServerUpdate>& rCoordUpdates);
void ReconcileRollback(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick);
int64_t ReconcileFindReplayRange(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick);
void ReconcileReplay(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick, int64_t iMaxConsecutive);
void ReconcileCatchUp(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick);
void ReconcileInjectPendingFullState(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork);
bool ReconcileInjectLateConfirmedCoord(CoordReconcileWork& rWork, int64_t iTick, int64_t iMinConfirmedTick);
bool ReconcileValidateCrcs(ReconcileContext& rReconcileContext, int64_t iTick, const std::unordered_map<engine::GridCoord, engine::CoordFrames::CoordServerUpdate>& rFrameCoordUpdates, const std::unordered_set<engine::GridCoord>& rGapCoords);
void ReconcilePruneInactiveFrames(ReconcileContext& rReconcileContext);

} // namespace game

#endif // BT_CLIENT
