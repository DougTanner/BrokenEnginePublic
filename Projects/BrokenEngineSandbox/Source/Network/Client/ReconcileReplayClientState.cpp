#include "Game.h"

#include "Network/Client/ReconcileReplay.h"

#include "Network/Client/ClientReconciler.h"
#include "Frame/Collections/Players/Players.h"

namespace game
{

#if defined(BT_CLIENT)

static std::optional<engine::global_id_t> FindMatchingPlayerInCoord(std::span<const CoordWork> works, engine::GridCoord destination, engine::global_id_t globalPlayerId)
{
	for (const CoordWork& rDestWork : works)
	{
		if (rDestWork.coord != destination)
		{
			continue;
		}

		const engine::CoordFrames& rDestFrames = *rDestWork.pFrames;
		const CoordScratch& rDestScratch = rDestWork.scratch;

		const Frame* pDestFrame = nullptr;
		if (rDestScratch.iReplayStackCount > 0)
		{
			pDestFrame = rDestScratch.replayStack[rDestScratch.iReplayStackCount - 1];
		}
		else if (rDestScratch.bCrcFastPath && rDestScratch.iNewConfirmedOffset >= 0)
		{
			// iNewConfirmedOffset is the new HEAD (may sit kiRenderBehindTicks slots before the
			// confirmed frame when render-behind retention is active); iNewConfirmedInnerOffset
			// is the delta from that head to the confirmed frame.
			int64_t iConfirmedPhysical = SnapshotIndex(rDestScratch.iNewConfirmedOffset, rDestScratch.iNewConfirmedInnerOffset);
			pDestFrame = rDestFrames.snapshots[iConfirmedPhysical].get();
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
				LOG(kNetwork, kVerbose, "ReconcileUpdateClientState Transfer matched GlobalPlayerId: {} Coord: ({},{})", globalPlayerId, destination.x, destination.y);
				return globalPlayerId;
			}
		}
		LOG(kNetwork, kVerbose, "ReconcileUpdateClientState Transfer global ID match failed Coord: ({},{}) PlayerCount: {}", destination.x, destination.y, rDestFrame.postRender.pPlayers->iCount);
		break;
	}
	return std::nullopt;
}

void ReconcileUpdateClientState(std::span<const CoordWork> works, const ReconcileInputs& rInputs, bool bAnyFullReplay, ConfirmedClientState& rInOutState)
{
	ConfirmedClientState clientState = rInOutState;

	// Advance fCurrentTime based on client coord's reconciliation result
	for (const CoordWork& rWork : works)
	{
		if (rWork.coord != clientState.clientGridCoord)
		{
			continue;
		}

		const engine::CoordFrames& rFrames = *rWork.pFrames;
		const CoordScratch& rScratch = rWork.scratch;

		if (!rScratch.bCrcFastPath && rScratch.iReplayStackCount > 0)
		{
			// Client coord did full replay: use replay tip's fCurrentTime
			clientState.fCurrentTime = rScratch.replayStack[rScratch.iReplayStackCount - 1]->interpolate.fCurrentTime;
		}
		else
		{
			// Client coord fast-pathed: read time from confirmed snapshot.
			// iNewConfirmedOffset is the new HEAD; iNewConfirmedInnerOffset is the delta to confirmed.
			int64_t iPhysical = (rScratch.iNewConfirmedOffset >= 0)
				? SnapshotIndex(rScratch.iNewConfirmedOffset, rScratch.iNewConfirmedInnerOffset)
				: SnapshotIndex(rFrames.iSnapshotHead, rFrames.iConfirmedOffset);
			if (rFrames.snapshots[iPhysical] != nullptr)
			{
				clientState.fCurrentTime = rFrames.snapshots[iPhysical]->interpolate.fCurrentTime;
				// Advance to target tick so fCurrentTime matches iTickCounter when SetCurrentTime is called
				int64_t iConfirmedTick = (rScratch.iNewConfirmedTick >= 0) ? rScratch.iNewConfirmedTick : rFrames.iConfirmedTick;
				for (int64_t i = iConfirmedTick; i < rInputs.iTargetTick; ++i)
				{
					clientState.fCurrentTime += kfDeltaTime;
				}
			}
		}
		break;
	}

	if (bAnyFullReplay)
	{
		// Scan full-replay coords for client migration via transfer requests
		for (const CoordWork& rWork : works)
		{
			const CoordScratch& rScratch = rWork.scratch;
			if (rScratch.bCrcFastPath)
			{
				continue;
			}

			// replayStack[0] is the confirmed frame; scan from index 1 onwards
			for (int64_t i = 1; i < rScratch.iReplayStackCount; ++i)
			{
				const Frame& rFrame = *rScratch.replayStack[i];
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

					if (std::abs(rRequest.iDeltaX) > 1 || std::abs(rRequest.iDeltaY) > 1) [[unlikely]]
					{
						LOG(kDefault, kError,
							"ReconcileUpdateClientState Transfer delta spans more than one grid cell Tick: {} Source: ({},{}) Delta: ({},{}) GlobalPlayerId: {}",
							rFrame.interpolate.iTick,
							rWork.coord.x, rWork.coord.y,
							static_cast<int32_t>(rRequest.iDeltaX), static_cast<int32_t>(rRequest.iDeltaY),
							clientState.clientGlobalPlayerId);
						DEBUG_BREAK();
					}
					engine::GridCoord destination {rWork.coord.x + rRequest.iDeltaX, rWork.coord.y + rRequest.iDeltaY};
					LOG(kNetwork, kVerbose, "ReconcileUpdateClientState TransferPlayer GlobalPlayerId: {} Source: ({},{}) Dest: ({},{})", clientState.clientGlobalPlayerId, rWork.coord.x, rWork.coord.y, destination.x, destination.y);
					clientState.clientGridCoord = destination;
					clientState.fPreviousClientArmor = rRequest.data.fHealth;

					std::optional<engine::global_id_t> matchedId = FindMatchingPlayerInCoord(works, destination, clientState.clientGlobalPlayerId);
					if (matchedId.has_value())
					{
						clientState.clientGlobalPlayerId = *matchedId;
					}
				}
			}
		}
	}

	rInOutState = clientState;
}

#endif // BT_CLIENT

} // namespace game
