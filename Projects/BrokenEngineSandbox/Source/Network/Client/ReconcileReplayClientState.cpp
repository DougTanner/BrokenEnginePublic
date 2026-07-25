#include "Game.h"

#include "Network/Client/ReconcileReplay.h"

#include "Network/Client/ClientReconciler.h"
#include "Frame/Collections/Players/Players.h"

namespace game
{

#if defined(BT_CLIENT)

static bool FindMatchingPlayerInCoord(std::span<const CoordWork> works, engine::GridCoord destination, engine::global_id_t globalPlayerId)
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
		else if ((rDestScratch.flags & ReconcileScratchFlags::kCrcFastPath) && rDestScratch.outputLayout.iHead >= 0)
		{
			int64_t iConfirmedPhysical = SnapshotIndex(rDestScratch.outputLayout.iHead, rDestScratch.outputLayout.iConfirmedInner);
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
				return true;
			}
		}
		LOG(kNetwork, kVerbose, "ReconcileUpdateClientState Transfer global ID match failed Coord: ({},{}) PlayerCount: {}", destination.x, destination.y, rDestFrame.postRender.pPlayers->iCount);
		break;
	}
	return false;
}

void ReconcileUpdateClientState(std::span<const CoordWork> works, bool bAnyFullReplay, ConfirmedClientState& rInOutState)
{
	ConfirmedClientState clientState = rInOutState;

	if (bAnyFullReplay)
	{
		// Scan full-replay coords for client migration via transfer requests
		for (const CoordWork& rWork : works)
		{
			const CoordScratch& rScratch = rWork.scratch;
			if (rScratch.flags & ReconcileScratchFlags::kCrcFastPath)
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
					clientState.fPreviousClientArmor = rRequest.data.fHealth;

					FindMatchingPlayerInCoord(works, destination, clientState.clientGlobalPlayerId);
				}
			}
		}
	}

	rInOutState = clientState;
}

#endif // BT_CLIENT

} // namespace game
