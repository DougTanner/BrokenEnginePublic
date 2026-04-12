#include "Pch.h"

#include "Network/Server/ServerTransferManager.h"

#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Game.h"
#include "Network/Server/ServerFleetManager.h"
#include "Network/Server/ServerSession.h"

namespace game
{

#if defined(BT_SERVER)

struct ClientTransferInfo
{
	engine::global_id_t globalPlayerId {};
	engine::GridCoord destination {};
	engine::ClientGuid clientGuid {};
};

static engine::ClientGuid TransferDataClientGuid(const TransferData& rData)
{
	return {rData.uiClientGuidHigh, rData.uiClientGuidLow};
}

void ServerTransferManager::CollectTransfers(std::vector<ClientTransferInfo>& rClientTransfers)
{
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		Frame& rNextFrame = gpGame->NextFrame(rCoord);
		if (rNextFrame.postRender.transferRequests.empty())
		{
			continue;
		}

		for (const TransferRequest& rRequest : rNextFrame.postRender.transferRequests)
		{
			engine::GridCoord destination {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};

			auto it = gpGame->mCoordFrames.find(destination);
			if (it == gpGame->mCoordFrames.end() || it->second.pNext == nullptr)
			{
				gpGame->CreateFrameAtCoord(destination);
				engine::CoordFrames& rFrames = gpGame->mCoordFrames.at(destination);
				rFrames.pNext = std::make_unique<Frame>();
				std::swap(rFrames.pCurrent, rFrames.pNext);
				it = gpGame->mCoordFrames.find(destination);
			}

			mTransfers.try_emplace(destination).first->second.push_back({
				.eType = rRequest.eType,
				.data = StatusChangeData{rRequest.data},
			});

			if (rRequest.eType == StatusChangeType::kTransferPlayer && rRequest.data.globalPlayerId.IsValid())
			{
				rClientTransfers.push_back({
					.globalPlayerId = rRequest.data.globalPlayerId,
					.destination = destination,
					.clientGuid = TransferDataClientGuid(rRequest.data),
				});
			}
		}
	}
}

void ServerTransferManager::SortTransfersByType()
{
	for (auto& [rCoord, rTransfers] : mTransfers)
	{
		std::ranges::sort(rTransfers, [](const StatusChange& rLeft, const StatusChange& rRight)
		{
			return rLeft.eType < rRight.eType;
		});
	}
}

void ServerTransferManager::SpawnTransfers()
{
	for (auto& [rCoord, rTransfers] : mTransfers)
	{
		Frame& rDestFrame = *gpGame->mCoordFrames.at(rCoord).pNext;
		for (const StatusChange& rTransfer : rTransfers)
		{
			TransferData data = std::get<TransferData>(rTransfer.data);
			SpawnTransfer(rDestFrame, rTransfer.eType, data, gpGame->PlayerAlignment());
		}
	}
}

void ServerTransferManager::TrackClientTransfers(const std::vector<ClientTransferInfo>& rClientTransfers)
{
	for (const ClientTransferInfo& rClientTransfer : rClientTransfers)
	{
		Frame& rDestFrame = *gpGame->mCoordFrames.at(rClientTransfer.destination).pNext;
		PlayersPostRender& rDestPlayers = *rDestFrame.postRender.pPlayers;

		// Find the transferred player in destination by scanning pGlobalPlayerIds
		int64_t iNewIndex = -1;
		for (int64_t i = 0; i < rDestPlayers.iCount; ++i)
		{
			if (rDestPlayers.pGlobalPlayerIds[i] == rClientTransfer.globalPlayerId)
			{
				iNewIndex = i;
				break;
			}
		}
		if (iNewIndex < 0)
		{
			continue;
		}

		bool bFoundClient = false;
		std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();
		for (engine::ClientConnection& rClient : rClients)
		{
			// Find the client that owns this global ID
			std::vector<engine::global_id_t>& rClientOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(rClient.iClientId).first->second;
			for (int64_t k = 0; k < std::ssize(rClientOwnedIds); ++k)
			{
				if (rClientOwnedIds.at(k) == rClientTransfer.globalPlayerId)
				{
					rClient.authorizedCoords.at(k) = rClientTransfer.destination;
					mPendingSubscriptionUpdates.push_back({
						.iClientId = rClient.iClientId,
						.newCoord = rClientTransfer.destination,
						.globalPlayerId = rClientTransfer.globalPlayerId,
					});

					// Copy client GUID to the new player entity in the destination frame
					rDestPlayers.pClientGuids[iNewIndex] = rClient.clientGuid;

					gpServerSession->mpFleetManager->OnPlayerTransferred(rClient.clientGuid, rClientTransfer.globalPlayerId, rClientTransfer.destination);

					bFoundClient = true;
					break;
				}
			}
			if (bFoundClient)
			{
				break;
			}
		}

		// Orphaned players (client disconnected): preserve GUID and update fleet
		if (!bFoundClient && !rClientTransfer.clientGuid.IsEmpty())
		{
			rDestPlayers.pClientGuids[iNewIndex] = rClientTransfer.clientGuid;
			gpServerSession->mpFleetManager->OnPlayerTransferred(rClientTransfer.clientGuid, rClientTransfer.globalPlayerId, rClientTransfer.destination);
		}
	}
}

void ServerTransferManager::HarvestTransfers()
{
	// Heap: Transfer spawns into destination frames, which may grow SOA buffers and update idToIndexMaps
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mTransfers.clear();
	std::vector<ClientTransferInfo> clientTransfers;

	CollectTransfers(clientTransfers);
	SortTransfersByType();

	// Log 5: capture pre-transfer CRCs from frame state (populated by RunFrameTick
	// before HarvestTransfers runs). Zero extra Crcs() calls - just read.
	std::unordered_map<engine::GridCoord, common::crc_t> preCrcs;
	preCrcs.reserve(mTransfers.size());
	for (const auto& [rCoord, rTransfers] : mTransfers)
	{
		const Frame& rDestFrame = *gpGame->mCoordFrames.at(rCoord).pNext;
		preCrcs.emplace(rCoord, rDestFrame.postRender.sharedCrc);
	}

	SpawnTransfers();

	// Recompute CRCs for destination frames after transfers modified them
	// (RunFrameTick computed CRCs before HarvestTransfers spawned entities)
	for (const auto& [rCoord, rTransfers] : mTransfers)
	{
		Frame& rDestFrame = *gpGame->mCoordFrames.at(rCoord).pNext;
		rDestFrame.postRender.sharedCrc = rDestFrame.Crcs();

		char acCrcPre[20] {}, acCrcPost[20] {};
		common::ToHex(std::span<char, 20>(acCrcPre), preCrcs.at(rCoord));
		common::ToHex(std::span<char, 20>(acCrcPost), rDestFrame.postRender.sharedCrc);

		char acPlayerIds[192] {};
		int64_t iPlayerIdCount = 0;
		int64_t iPos = 0;
		for (const StatusChange& rTransfer : rTransfers)
		{
			if (rTransfer.eType == StatusChangeType::kTransferPlayer && iPlayerIdCount < 8)
			{
				if (iPlayerIdCount > 0) { acPlayerIds[iPos++] = ','; acPlayerIds[iPos++] = ' '; }
				iPos += snprintf(acPlayerIds + iPos, sizeof(acPlayerIds) - iPos, "%lld", std::get<TransferData>(rTransfer.data).globalPlayerId.iValue);
				++iPlayerIdCount;
			}
		}

		if (iPlayerIdCount > 0)
		{
			LOG(kNetwork, kVerbose, "Server SpawnTransfers Dest: ({},{}) TransferCount: {} PlayerCount: {} BlasterCount: {} SpaceshipCount: {} MissileCount: {} CrcPre: {} CrcPost: {} PlayerIds: [{}]", rCoord.x, rCoord.y, rTransfers.size(), rDestFrame.postRender.pPlayers->iCount, rDestFrame.postRender.pBlasters->iCount, rDestFrame.postRender.pSpaceships->iCount, rDestFrame.postRender.pMissiles->iCount, acCrcPre, acCrcPost, acPlayerIds);
		}
		else
		{
			LOG(kNetwork, kDebug, "Server SpawnTransfers Dest: ({},{}) TransferCount: {} PlayerCount: {} BlasterCount: {} SpaceshipCount: {} MissileCount: {} CrcPre: {} CrcPost: {}", rCoord.x, rCoord.y, rTransfers.size(), rDestFrame.postRender.pPlayers->iCount, rDestFrame.postRender.pBlasters->iCount, rDestFrame.postRender.pSpaceships->iCount, rDestFrame.postRender.pMissiles->iCount, acCrcPre, acCrcPost);
		}
	}

	TrackClientTransfers(clientTransfers);
}

void ServerTransferManager::ResetState()
{
	mTransfers.clear();
	mPendingSubscriptionUpdates.clear();
}

bool ServerTransferManager::HasPendingSubscriptionUpdate(int64_t iClientId) const
{
	return std::ranges::contains(mPendingSubscriptionUpdates, iClientId, &SubscriptionUpdate::iClientId);
}

#endif // BT_SERVER

} // namespace game
