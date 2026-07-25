#include "Pch.h"

#include "Network/Server/ClientPlayerRegistry.h"

#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/PlayerEvents.h"
#include "Network/Server/ServerSession.h"

#if defined(BT_SERVER)

namespace game
{

void ClientPlayerRegistry::Add(int64_t iClientId, engine::global_id_t globalId, engine::GridCoord coord)
{
	mOwned.try_emplace(iClientId).first->second.push_back({.globalId = globalId, .coord = coord});

	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient != nullptr)
	{
		pClient->authorizedCoords.push_back(coord);
	}
}

void ClientPlayerRegistry::RemoveAt(int64_t iClientId, int64_t iIndex)
{
	auto it = mOwned.find(iClientId);
	if (it == mOwned.end())
	{
		return;
	}

	it->second.erase(it->second.begin() + iIndex);

	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient != nullptr)
	{
		pClient->authorizedCoords.erase(pClient->authorizedCoords.begin() + iIndex);
	}
}

void ClientPlayerRegistry::UpdateCoord(int64_t iClientId, engine::global_id_t globalId, engine::GridCoord newCoord)
{
	auto it = mOwned.find(iClientId);
	if (it == mOwned.end())
	{
		return;
	}

	for (int64_t i = 0; i < std::ssize(it->second); ++i)
	{
		OwnedPlayer& rOwnedPlayer = it->second.at(i);
		if (rOwnedPlayer.globalId != globalId)
		{
			continue;
		}

		rOwnedPlayer.coord = newCoord;
		engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
		if (pClient != nullptr)
		{
			pClient->authorizedCoords.at(i) = newCoord;
		}
		return;
	}
}

std::span<const OwnedPlayer> ClientPlayerRegistry::Owned(int64_t iClientId) const
{
	auto it = mOwned.find(iClientId);
	if (it == mOwned.end())
	{
		return {};
	}
	return it->second;
}

void ClientPlayerRegistry::Remove(int64_t iClientId)
{
	mOwned.erase(iClientId);
}

void ClientPlayerRegistry::Clear(int64_t iClientId)
{
	mOwned.try_emplace(iClientId).first->second.clear();

	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient != nullptr)
	{
		pClient->authorizedCoords.clear();
	}
}

int64_t ClientPlayerRegistry::RelinkFromFrames(int64_t iClientId, const engine::ClientGuid& rGuid, RelinkContext eContext)
{
	if (rGuid.IsEmpty())
	{
		return 0;
	}

	std::vector<OwnedPlayer> relinkEntries;
	relinkEntries.reserve(gpGame->mCoordFrames.size());
	for (const auto& [rCoord, rFrames] : gpGame->mCoordFrames)
	{
		const PlayersPostRender& rPlayers = *rFrames.pCurrent->postRender.pPlayers;
		for (int64_t i = 0; i < rPlayers.iCount; ++i)
		{
			if (rPlayers.pClientGuids[i] == rGuid)
			{
				relinkEntries.push_back({.globalId = rPlayers.pGlobalPlayerIds[i], .coord = rCoord});
			}
		}
	}

	std::ranges::sort(relinkEntries, [](const OwnedPlayer& rLeft, const OwnedPlayer& rRight)
	{
		return rLeft.globalId.iValue < rRight.globalId.iValue;
	});

	mOwned.try_emplace(iClientId).first->second.reserve(relinkEntries.size());
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient != nullptr)
	{
		pClient->authorizedCoords.reserve(relinkEntries.size());
	}

	for (const OwnedPlayer& rEntry : relinkEntries)
	{
		Add(iClientId, rEntry.globalId, rEntry.coord);
		gpServerSession->SendAssignPlayer(iClientId, rEntry.globalId, rEntry.coord);
		gpServerSession->SendPlayerState(iClientId, PlayerStateWireType::kSpawned, rEntry.globalId.iValue, rEntry.coord);
		if (eContext == RelinkContext::kConnect)
		{
			LOG(kNetwork, kVerbose, "ServerClientManager::NewClients Re-linked Client: {} GlobalPlayer: {} Coord: ({},{})", iClientId, rEntry.globalId, rEntry.coord.x, rEntry.coord.y);
		}
		else
		{
			LOG(kDefault, kDebug, "ServerSession::ResetClientsForLoad Re-linked Client: {} GlobalPlayer: {} Coord: ({},{})", iClientId, rEntry.globalId, rEntry.coord.x, rEntry.coord.y);
		}
	}

	return static_cast<int64_t>(relinkEntries.size());
}

} // namespace game

#endif // BT_SERVER
