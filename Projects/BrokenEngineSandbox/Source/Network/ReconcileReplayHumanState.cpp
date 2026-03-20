#include "Game.h"

#include "Network/ReconcileReplay.h"

#include "Network/ClientReconciler.h"
#include "Frame/Collections/Players/Players.h"

namespace game
{

#if defined(BT_CLIENT)

static std::optional<player_t> FindMatchingPlayerInCoord(const ReconcileContext& rReconcileContext, engine::GridCoord destination, XMVECTOR vecPosition)
{
	for (const CoordReconcileWork& rDestWork : rReconcileContext.coordWork)
	{
		if (rDestWork.coord != destination)
		{
			continue;
		}

		const Frame* pDestFrame = nullptr;
		if (rDestWork.iReplayStackCount > 0)
		{
			pDestFrame = rDestWork.replayStack[rDestWork.iReplayStackCount - 1];
		}
		else if (rDestWork.bCrcFastPath && rDestWork.iNewConfirmedOffset >= 0)
		{
			pDestFrame = rDestWork.snapshots[rDestWork.iNewConfirmedOffset].get();
		}
		if (pDestFrame == nullptr)
		{
			continue;
		}
		const Frame& rDestFrame = *pDestFrame;
		for (int64_t j = 0; j < rDestFrame.postRender.pPlayers->iCount; ++j)
		{
			if (XMVector4Equal(rDestFrame.interpolate.pPlayers->pVecPositions[j], vecPosition))
			{
				// DT TEMP
				Log(kLogNetwork, "ReconcileUpdateHumanState Transfer matched NewPlayerId: {} Coord: ({},{})", rDestFrame.postRender.pPlayers->puiIds[j].ToUuid().Value(), destination.x, destination.y);
				return rDestFrame.postRender.pPlayers->puiIds[j];
			}
		}
		Log(kLogNetwork, "ReconcileUpdateHumanState Transfer position match failed Coord: ({},{}) PlayerCount: {}", destination.x, destination.y, rDestFrame.postRender.pPlayers->iCount);
		break;
	}
	return std::nullopt;
}

void ReconcileUpdateHumanState(ReconcileContext& rReconcileContext)
{
	ConfirmedHumanState humanState = rReconcileContext.confirmedHumanState;

	// Advance fCurrentTime based on human coord's reconciliation result
	for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.coord != humanState.humanGridCoord)
		{
			continue;
		}

		if (!rWork.bCrcFastPath && rWork.iReplayStackCount > 0)
		{
			// Human coord did full replay: use replay tip's fCurrentTime
			humanState.fCurrentTime = rWork.replayStack[rWork.iReplayStackCount - 1]->interpolate.fCurrentTime;
		}
		else
		{
			// Human coord fast-pathed: read time from confirmed snapshot
			int64_t iPhysical = (rWork.iNewConfirmedOffset >= 0)
				? rWork.iNewConfirmedOffset
				: SnapshotIndex(rWork.iSnapshotHead, rWork.iConfirmedOffset);
			if (rWork.snapshots[iPhysical] != nullptr)
			{
				humanState.fCurrentTime = rWork.snapshots[iPhysical]->interpolate.fCurrentTime;
				// Advance to target tick so fCurrentTime matches iTickCounter when SetCurrentTime is called
				int64_t iConfirmedTick = (rWork.iNewConfirmedTick >= 0) ? rWork.iNewConfirmedTick : rWork.iConfirmedTick;
				for (int64_t i = iConfirmedTick; i < rReconcileContext.iTargetTick; ++i)
				{
					humanState.fCurrentTime += kfDeltaTime;
				}
			}
		}
		break;
	}

	if (rReconcileContext.bAnyFullReplay)
	{
		// Scan full-replay coords for human migration via transfer requests
		for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.bCrcFastPath)
			{
				continue;
			}

			// replayStack[0] is the confirmed frame; scan from index 1 onwards
			for (int64_t i = 1; i < rWork.iReplayStackCount; ++i)
			{
				const Frame& rFrame = *rWork.replayStack[i];
				for (const TransferRequest& rRequest : rFrame.postRender.transferRequests)
				{
					if (rRequest.eType != StatusChangeType::kTransferPlayer)
					{
						continue;
					}
					if (!humanState.humanPlayerId.IsValid())
					{
						continue;
					}
					if (rRequest.iEntityId != humanState.humanPlayerId.ToUuid().Value())
					{
						continue;
					}

					engine::GridCoord destination {rWork.coord.x + rRequest.iDeltaX, rWork.coord.y + rRequest.iDeltaY};
					// DT TEMP
					Log(kLogNetwork, "ReconcileUpdateHumanState TransferPlayer PlayerId: {} Source: ({},{}) Dest: ({},{})", humanState.humanPlayerId.ToUuid().Value(), rWork.coord.x, rWork.coord.y, destination.x, destination.y);
					humanState.humanGridCoord = destination;
					humanState.fPreviousHumanArmor = rRequest.data.fHealth;

					std::optional<player_t> matchedId = FindMatchingPlayerInCoord(rReconcileContext, destination, rRequest.data.vecPosition);
					if (matchedId.has_value())
					{
						humanState.humanPlayerId = *matchedId;
					}
				}
			}
		}
	}

	rReconcileContext.newConfirmedHumanState = humanState;
}

#endif // BT_CLIENT

} // namespace game
