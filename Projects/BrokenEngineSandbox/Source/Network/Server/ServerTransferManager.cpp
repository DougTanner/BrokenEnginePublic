#include "Pch.h"

#include "Network/Server/ServerTransferManager.h"

#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Game.h"
#include "Network/Server/ServerFleetManager.h"
#include "Network/Server/ServerSession.h"
#include "SpawnTransfer.h"

namespace game
{

#if defined(BT_SERVER)

static engine::ClientGuid TransferDataClientGuid(const TransferData& rData)
{
	return {rData.uiClientGuidHigh, rData.uiClientGuidLow};
}

// A destination is "live" if it has a committed player or any client actively subscribes to it.
// Deliberately does not consult mActiveCoords: ComputeActiveSet force-adds kOriginCoord (0,0)
// every tick as an initial-fleet-spawn bootstrap, so membership there doesn't imply anyone is
// watching. This check is used to decide whether non-Player transfers should be dropped instead
// of materializing ghost entities that clients can't see.
static bool IsDestinationLive(engine::GridCoord destination)
{
	auto destinationIt = gpGame->mCoordFrames.find(destination);
	if (destinationIt != gpGame->mCoordFrames.end() && destinationIt->second.pCurrent != nullptr &&
		destinationIt->second.pCurrent->postRender.pPlayers->iCount > 0)
	{
		return true;
	}
	for (const engine::ClientConnection& rClient : engine::gpServer->mClients)
	{
		for (int64_t i = 0; i < std::ssize(rClient.slots); ++i)
		{
			if ((rClient.slots.at(i).subscription.flags & engine::SubscriptionFlags::kActive) && rClient.slots.at(i).subscription.coord == destination)
			{
				return true;
			}
		}
	}
	return false;
}

void ServerTransferManager::CollectTransfers(common::ScopedWorkbufferArena& rTransfersArena)
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
			// Transfer destinations must be adjacent (±1 in each axis — Chebyshev distance ≤ 1);
			// anything larger would teleport the entity. Log carries Source + Delta + Dest so the
			// landing coord reads off directly without mental arithmetic.
			if (std::abs(rRequest.iDeltaX) > 1 || std::abs(rRequest.iDeltaY) > 1) [[unlikely]]
			{
				LOG(kDefault, kError, "Transfer delta spans more than one grid cell Tick: {} Source: ({},{}) Delta: ({},{}) Dest: ({},{}) Type: {} Position: {} Velocity: {}", rNextFrame.interpolate.iTick, rCoord.x, rCoord.y, static_cast<int32_t>(rRequest.iDeltaX), static_cast<int32_t>(rRequest.iDeltaY), rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY, StatusChangeTypeName(rRequest.eType), common::WbV2(rRequest.data.vecPosition, 1), common::WbV2(rRequest.data.vecVelocity, 1));
				DEBUG_BREAK();
			}

			engine::GridCoord destination {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};

			// Drop non-Player transfers (spaceships, blasters, missiles) whose destination is not
			// live. kTransferPlayer is always allowed — the player's arrival IS the subscription.
			// Sibling-subscribed empty cells (client watching but no player present) remain live
			// and receive transfers normally so clients don't see entities vanish at cell
			// boundaries.
			if (rRequest.eType != StatusChangeType::kTransferPlayer && !IsDestinationLive(destination))
			{
				LOG(kNetwork, kVerbose, "Dropping transfer to unsubscribed Frame Tick: {} Source: ({},{}) Dest: ({},{}) Type: {}", rNextFrame.interpolate.iTick, rCoord.x, rCoord.y, destination.x, destination.y, StatusChangeTypeName(rRequest.eType));
				continue;
			}

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
				rTransfersArena.PushBack(ClientTransferInfo{
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
			// Invariant: only kTransferPlayer may spawn into a non-live destination. CollectTransfers
			// drops all other types whose destination fails IsDestinationLive, so reaching here with
			// a non-Player transfer means CollectTransfers and SpawnTransfers disagree on liveness
			// (e.g. a subscription was dropped or a player destroyed between the two phases) and
			// entities would pile up in a cell no client can observe.
			if (rTransfer.eType != StatusChangeType::kTransferPlayer && !IsDestinationLive(rCoord)) [[unlikely]]
			{
				LOG(kDefault, kError, "Non-Player transfer reached Spawn for non-live Frame Tick: {} Dest: ({},{}) Type: {}", rDestFrame.interpolate.iTick, rCoord.x, rCoord.y, StatusChangeTypeName(rTransfer.eType));
				DEBUG_BREAK();
			}

			TransferData data = std::get<TransferData>(rTransfer.data);
			SpawnTransfer(rDestFrame, rTransfer.eType, data, gpGame->PlayerAlignment());
		}
	}
}

void ServerTransferManager::TrackClientTransfers(std::span<const ClientTransferInfo> clientTransfers)
{
	for (const ClientTransferInfo& rClientTransfer : clientTransfers)
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
		std::vector<engine::ClientConnection>& rClients = engine::gpServer->mClients;
		for (engine::ClientConnection& rClient : rClients)
		{
			// Find the client that owns this global ID
			for (const OwnedPlayer& rOwnedPlayer : gpServerSession->mClientPlayers.Owned(rClient.iClientId))
			{
				if (rOwnedPlayer.globalId == rClientTransfer.globalPlayerId)
				{
					gpServerSession->mClientPlayers.UpdateCoord(rClient.iClientId, rClientTransfer.globalPlayerId, rClientTransfer.destination);
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
	ScopedSuppressAllocationTracking suppress;

	mTransfers.clear();

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena transfersArena = rWorkbuffer.Push();
	CollectTransfers(transfersArena);
	std::span<const ClientTransferInfo> clientTransfers = transfersArena.Span<const ClientTransferInfo>();
	for (auto& [rCoord, rTransfers] : mReplayTransferFixtures)
	{
		auto it = gpGame->mCoordFrames.find(rCoord);
		if (it == gpGame->mCoordFrames.end() || it->second.pNext == nullptr)
		{
			gpGame->CreateFrameAtCoord(rCoord);
			engine::CoordFrames& rFrames = gpGame->mCoordFrames.at(rCoord);
			rFrames.pNext = std::make_unique<Frame>();
			std::swap(rFrames.pCurrent, rFrames.pNext);
		}

		auto& rDestinationTransfers = mTransfers.try_emplace(rCoord).first->second;
		rDestinationTransfers.insert(rDestinationTransfers.end(), std::make_move_iterator(rTransfers.begin()), std::make_move_iterator(rTransfers.end()));
	}
	mReplayTransferFixtures.clear();

	SortTransfersByType();

	for (const auto& [rCoord, rTransfers] : mTransfers)
	{
		gpGame->CaptureHarvestedTransfers(rCoord, rTransfers, *gpGame->mCoordFrames.at(rCoord).pNext);
	}

	// Log 5: capture pre-transfer CRCs from frame state (populated by RunFrameTick
	// before HarvestTransfers runs). Zero extra Crcs() calls - just read. preCrcs is indexed by
	// mTransfers enumeration order, stable across SpawnTransfers (which only mutates frames, not the map).
	auto pPreCrcs = rWorkbuffer.PushBuffer<common::crc_t*>(std::ssize(mTransfers) * static_cast<int64_t>(sizeof(common::crc_t)));
	int64_t iPreCrcIndex = 0;
	for (const auto& [rCoord, rTransfers] : mTransfers)
	{
		const Frame& rDestFrame = *gpGame->mCoordFrames.at(rCoord).pNext;
		pPreCrcs[iPreCrcIndex++] = rDestFrame.postRender.sharedCrc;
	}

	SpawnTransfers();

	// Recompute CRCs for destination frames after transfers modified them
	// (RunFrameTick computed CRCs before HarvestTransfers spawned entities)
	iPreCrcIndex = 0;
	for (const auto& [rCoord, rTransfers] : mTransfers)
	{
		Frame& rDestFrame = *gpGame->mCoordFrames.at(rCoord).pNext;
		rDestFrame.postRender.sharedCrc = rDestFrame.Crcs();

		char acCrcPre[20] {}, acCrcPost[20] {};
		common::ToHex(std::span<char, 20>(acCrcPre), pPreCrcs[iPreCrcIndex++]);
		common::ToHex(std::span<char, 20>(acCrcPost), rDestFrame.postRender.sharedCrc);

		char acPlayerIds[192] {};
		int64_t iPlayerIdCount = 0;
		size_t iPos = 0;
		for (const StatusChange& rTransfer : rTransfers)
		{
			if (rTransfer.eType != StatusChangeType::kTransferPlayer || iPlayerIdCount >= 8)
			{
				continue;
			}

			constexpr size_t kiReserve = 24; // ", " + max 20-digit int64
			if (iPos + kiReserve > sizeof(acPlayerIds))
			{
				break;
			}

			if (iPlayerIdCount > 0)
			{
				acPlayerIds[iPos++] = ',';
				acPlayerIds[iPos++] = ' ';
			}
			int iWritten = std::snprintf(acPlayerIds + iPos, sizeof(acPlayerIds) - iPos, "%lld", std::get<TransferData>(rTransfer.data).globalPlayerId.iValue);
			if (iWritten <= 0)
			{
				break;
			}
			iPos += static_cast<size_t>(iWritten);
			++iPlayerIdCount;
		}

		if (iPlayerIdCount > 0)
		{
			LOG(kNetwork, kVerbose, "ServerTransferManager::SpawnTransfers Dest: ({},{}) TransferCount: {} PlayerCount: {} BlasterCount: {} SpaceshipCount: {} MissileCount: {} CrcPre: {} CrcPost: {} PlayerIds: [{}]", rCoord.x, rCoord.y, rTransfers.size(), rDestFrame.postRender.pPlayers->iCount, rDestFrame.postRender.pBlasters->iCount, rDestFrame.postRender.pSpaceships->iCount, rDestFrame.postRender.pMissiles->iCount, acCrcPre, acCrcPost, acPlayerIds);
		}
		else
		{
			bool bAnySubscribed = false;
			for (const engine::ClientConnection& rClient : engine::gpServer->mClients)
			{
				if (rClient.FindSlotForCoord(rCoord) >= 0)
				{
					bAnySubscribed = true;
					break;
				}
			}
			if (bAnySubscribed)
			{
				LOG(kNetwork, kVerbose, "ServerTransferManager::SpawnTransfers Dest: ({},{}) TransferCount: {} PlayerCount: {} BlasterCount: {} SpaceshipCount: {} MissileCount: {} CrcPre: {} CrcPost: {}", rCoord.x, rCoord.y, rTransfers.size(), rDestFrame.postRender.pPlayers->iCount, rDestFrame.postRender.pBlasters->iCount, rDestFrame.postRender.pSpaceships->iCount, rDestFrame.postRender.pMissiles->iCount, acCrcPre, acCrcPost);
			}
		}
	}

	TrackClientTransfers(clientTransfers);
}

void ServerTransferManager::ResetState()
{
	mTransfers.clear();
	mReplayTransferFixtures.clear();
	mPendingSubscriptionUpdates.clear();
}

bool ServerTransferManager::QueueReplayTransferFixture(engine::GridCoord destination, StatusChange transfer)
{
	if constexpr (!kbDebugInput)
	{
		return false;
	}

	if (!IsTransferType(transfer.eType) || !std::holds_alternative<TransferData>(transfer.data) ||
		(transfer.eType != StatusChangeType::kTransferPlayer && !IsDestinationLive(destination)))
	{
		return false;
	}

	mReplayTransferFixtures.try_emplace(destination).first->second.push_back(std::move(transfer));
	return true;
}

bool ServerTransferManager::HasPendingSubscriptionUpdate(int64_t iClientId) const
{
	return std::ranges::contains(mPendingSubscriptionUpdates, iClientId, &SubscriptionUpdate::iClientId);
}

#endif // BT_SERVER

} // namespace game
