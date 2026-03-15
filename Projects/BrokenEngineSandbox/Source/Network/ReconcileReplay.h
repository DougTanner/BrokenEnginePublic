#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct ReconcileContext;
struct CoordReconcileWork;

void ReconcileCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork);
void ReconcileUpdateHumanState(ReconcileContext& rReconcileContext);
void ReconcileInjectPendingFullState(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork);

} // namespace game

#endif // BT_CLIENT
