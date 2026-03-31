#include "Game.h"

#include "Network/ReconcileReplay.h"

#include "Network/ClientReconciler.h"
#include "Frame/Collections/Players/Players.h"

namespace game
{

#if defined(BT_CLIENT)

static std::optional<engine::global_player_t> FindMatchingPlayerInCoord(const ReconcileContext& rReconcileContext, engine::GridCoord destination, engine::global_player_t globalPlayerId)
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
			if (rDestFrame.postRender.pPlayers->pGlobalPlayerIds[j] == globalPlayerId)
			{
				Log(kLogNetwork, "ReconcileUpdateClientState Transfer matched GlobalPlayerId: {} Coord: ({},{})", globalPlayerId.iValue, destination.x, destination.y); // DT TEMP
				return globalPlayerId;
			}
		}
		Log(kLogNetwork, "ReconcileUpdateClientState Transfer global ID match failed Coord: ({},{}) PlayerCount: {}", destination.x, destination.y, rDestFrame.postRender.pPlayers->iCount);
		break;
	}
	return std::nullopt;
}

void ReconcileUpdateClientState(ReconcileContext& rReconcileContext)
{
	ConfirmedClientState clientState = rReconcileContext.confirmedClientState;

	// Advance fCurrentTime based on human coord's reconciliation result
	for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.coord != clientState.clientGridCoord)
		{
			continue;
		}

		if (!rWork.bCrcFastPath && rWork.iReplayStackCount > 0)
		{
			// Human coord did full replay: use replay tip's fCurrentTime
			clientState.fCurrentTime = rWork.replayStack[rWork.iReplayStackCount - 1]->interpolate.fCurrentTime;
		}
		else
		{
			// Human coord fast-pathed: read time from confirmed snapshot
			int64_t iPhysical = (rWork.iNewConfirmedOffset >= 0)
				? rWork.iNewConfirmedOffset
				: SnapshotIndex(rWork.iSnapshotHead, rWork.iConfirmedOffset);
			if (rWork.snapshots[iPhysical] != nullptr)
			{
				clientState.fCurrentTime = rWork.snapshots[iPhysical]->interpolate.fCurrentTime;
				// Advance to target tick so fCurrentTime matches iTickCounter when SetCurrentTime is called
				int64_t iConfirmedTick = (rWork.iNewConfirmedTick >= 0) ? rWork.iNewConfirmedTick : rWork.iConfirmedTick;
				for (int64_t i = iConfirmedTick; i < rReconcileContext.iTargetTick; ++i)
				{
					clientState.fCurrentTime += kfDeltaTime;
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
					if (!clientState.clientGlobalPlayerId.IsValid())
					{
						continue;
					}
					if (rRequest.data.globalPlayerId != clientState.clientGlobalPlayerId)
					{
						continue;
					}

					engine::GridCoord destination {rWork.coord.x + rRequest.iDeltaX, rWork.coord.y + rRequest.iDeltaY};
					Log(kLogNetwork, "ReconcileUpdateClientState TransferPlayer GlobalPlayerId: {} Source: ({},{}) Dest: ({},{})", clientState.clientGlobalPlayerId.iValue, rWork.coord.x, rWork.coord.y, destination.x, destination.y); // DT TEMP
					clientState.clientGridCoord = destination;
					clientState.fPreviousClientArmor = rRequest.data.fHealth;

					std::optional<engine::global_player_t> matchedId = FindMatchingPlayerInCoord(rReconcileContext, destination, clientState.clientGlobalPlayerId);
					if (matchedId.has_value())
					{
						clientState.clientGlobalPlayerId = *matchedId;
					}
				}
			}
		}
	}

	rReconcileContext.newConfirmedClientState = clientState;
}

#endif // BT_CLIENT

} // namespace game
