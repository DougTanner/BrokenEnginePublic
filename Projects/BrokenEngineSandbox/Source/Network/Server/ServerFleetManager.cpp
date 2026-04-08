#include "Pch.h"

#include "Network/Server/ServerFleetManager.h"

#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/GamePacketType.h"
#include "Network/PlayerEvents.h"
#include "Network/Server/ServerClientManager.h"
#include "Network/Server/ServerSession.h"

namespace game
{

#if defined(BT_SERVER)

void ServerFleetManager::ProcessCreateFleetRequests()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const PendingCreateFleetRequest& rRequest : mPendingCreateFleetRequests)
	{
		const engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		mClientFleets.try_emplace(rRequest.iClientId).first->second.emplace_back();
		Log(kLogNetwork, "ProcessCreateFleetRequests Client: {} FleetCount: {}", rRequest.iClientId, mClientFleets.at(rRequest.iClientId).size());
		SendFleetSyncToClient(rRequest.iClientId);
	}
}

void ServerFleetManager::ProcessSpawnIntoFleetRequests()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const PendingSpawnIntoFleetRequest& rRequest : mPendingSpawnIntoFleetRequests)
	{
		const engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		auto it = mClientFleets.find(rRequest.iClientId);
		if (it == mClientFleets.end() || rRequest.iFleetIndex < 0 || rRequest.iFleetIndex >= std::ssize(it->second))
		{
			continue;
		}

		gpServerSession->mpClientManager->QueueSpawnForClient(rRequest.iClientId, engine::kOriginCoord, rRequest.iFleetIndex, -1);
		Log(kLogNetwork, "ProcessSpawnIntoFleetRequests Client: {} Fleet: {}", rRequest.iClientId, rRequest.iFleetIndex);
	}
}

void ServerFleetManager::ProcessRespawnInFleetRequests()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const PendingRespawnInFleetRequest& rRequest : mPendingRespawnInFleetRequests)
	{
		const engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		auto it = mClientFleets.find(rRequest.iClientId);
		if (it == mClientFleets.end() || rRequest.iFleetIndex < 0 || rRequest.iFleetIndex >= std::ssize(it->second))
		{
			continue;
		}

		const Fleet& rFleet = it->second.at(static_cast<size_t>(rRequest.iFleetIndex));
		if (rRequest.iMemberIndex < 0 || rRequest.iMemberIndex >= std::ssize(rFleet.members))
		{
			continue;
		}

		if (rFleet.members.at(static_cast<size_t>(rRequest.iMemberIndex)).bAlive)
		{
			continue;
		}

		gpServerSession->mpClientManager->QueueSpawnForClient(rRequest.iClientId, engine::kOriginCoord, rRequest.iFleetIndex, rRequest.iMemberIndex);
		Log(kLogNetwork, "ProcessRespawnInFleetRequests Client: {} Fleet: {} Member: {}", rRequest.iClientId, rRequest.iFleetIndex, rRequest.iMemberIndex);
	}
}

void ServerFleetManager::ProcessFlagshipUpdates()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const PendingFlagshipUpdate& rUpdate : mPendingFlagshipUpdates)
	{
		engine::ClientConnection* pClient = engine::gpServer->FindClient(rUpdate.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		auto fleetIt = mClientFleets.find(rUpdate.iClientId);
		if (fleetIt == mClientFleets.end())
		{
			continue;
		}

		if (rUpdate.iFleetIndex >= std::ssize(fleetIt->second))
		{
			continue;
		}
		const Fleet& rFleet = fleetIt->second.at(static_cast<size_t>(rUpdate.iFleetIndex));

		if (rFleet.iFlagshipIndex >= std::ssize(rFleet.members))
		{
			continue;
		}

		// Send update to all alive members in this fleet
		for (int64_t i = 0; i < std::ssize(rFleet.members); ++i)
		{
			if (!rFleet.members.at(i).bAlive)
			{
				continue;
			}
			engine::global_id_t memberId = rFleet.members.at(i).globalPlayerId;
			bool bMemberIsFlagship = (i == rFleet.iFlagshipIndex);

			std::vector<engine::global_id_t>& rFlagshipOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(pClient->iClientId).first->second;
			for (int64_t j = 0; j < std::ssize(rFlagshipOwnedIds); ++j)
			{
				if (rFlagshipOwnedIds.at(j) != memberId)
				{
					continue;
				}
				engine::GridCoord memberCoord = pClient->authorizedCoords.at(j);

				auto frameInputIt = gpGame->mFrameInputs.find(memberCoord);
				if (frameInputIt == gpGame->mFrameInputs.end())
				{
					break;
				}
				if (!gpGame->mCoordFrames.contains(memberCoord))
				{
					break;
				}

				const PlayersPostRender& rPlayers = *gpGame->CurrentFrame(memberCoord).postRender.pPlayers;
				for (int64_t k = 0; k < rPlayers.iCount; ++k)
				{
					if (rPlayers.pGlobalPlayerIds[k] == memberId)
					{
						int64_t iPlayerUuid = rPlayers.puiIds[k].ToUuid().Value();
						engine::GridCoord injectedCoord = bMemberIsFlagship ? memberCoord : rUpdate.newFlagshipCoord;
						frameInputIt->second.statusChanges.push_back({
							.eType = StatusChangeType::kUpdateFlagshipCoord,
							.data = UpdateFlagshipCoordData {
								.iPlayerUuid = iPlayerUuid,
								.bIsFlagship = bMemberIsFlagship,
								.flagshipCoord = injectedCoord}
						});
						Log(kLogNetwork, kVerbose, "ProcessFlagshipUpdates Client: {} Fleet: {} GlobalId: {} Uuid: {} MemberCoord: ({},{}) IsFlagship: {} FlagshipCoord: ({},{})", rUpdate.iClientId, rUpdate.iFleetIndex, memberId.iValue, iPlayerUuid, memberCoord.x, memberCoord.y, bMemberIsFlagship, injectedCoord.x, injectedCoord.y);
						break;
					}
				}
				break;
			}
		}
	}
	mPendingFlagshipUpdates.clear();
}

void ServerFleetManager::SendFleetSyncToClient(int64_t iClientId)
{
	auto it = mClientFleets.find(iClientId);
	if (it != mClientFleets.end())
	{
		SendFleetSync(iClientId, it->second);
	}
	else
	{
		SendFleetSync(iClientId, {});
	}
}

void ServerFleetManager::SendFleetSync(int64_t iClientId, const std::vector<Fleet>& rFleets)
{
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	Log(kLogNetwork, kVerbose, "ServerFleetManager::SendFleetSync Client: {} Fleets: {}", iClientId, rFleets.size());

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][8B fleetCount] per fleet: [8B memberCount][8B iFlagshipIndex] per member: [8B globalPlayerId][1B bAlive]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kServerFleetSync));
	rWorkbuffer.PushBack<int64_t>(std::ssize(rFleets));
	for (const Fleet& rFleet : rFleets)
	{
		rWorkbuffer.PushBack<int64_t>(std::ssize(rFleet.members));
		rWorkbuffer.PushBack<int64_t>(rFleet.iFlagshipIndex);
		for (const FleetMember& rMember : rFleet.members)
		{
			rWorkbuffer.PushBack<int64_t>(rMember.globalPlayerId.iValue);
			rWorkbuffer.PushBack<uint8_t>(rMember.bAlive ? 1 : 0);
		}
	}

	engine::NetworkManager::SendPacket(pClient->pPeer, engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void ServerFleetManager::QueueCreateRequest(const PendingCreateFleetRequest& rRequest)
{
	mPendingCreateFleetRequests.push_back(rRequest);
}

void ServerFleetManager::QueueSpawnIntoRequest(const PendingSpawnIntoFleetRequest& rRequest)
{
	mPendingSpawnIntoFleetRequests.push_back(rRequest);
}

void ServerFleetManager::QueueRespawnRequest(const PendingRespawnInFleetRequest& rRequest)
{
	mPendingRespawnInFleetRequests.push_back(rRequest);
}

void ServerFleetManager::ClearPendingRequests()
{
	mPendingCreateFleetRequests.clear();
	mPendingSpawnIntoFleetRequests.clear();
	mPendingRespawnInFleetRequests.clear();
}

void ServerFleetManager::ShiftFlagshipAfterDeath(int64_t iClientId, int64_t iFleetIndex, Fleet& rFleet)
{
	int64_t iNewFlagship = -1;
	for (int64_t k = 1; k < std::ssize(rFleet.members); ++k)
	{
		int64_t iCandidate = (rFleet.iFlagshipIndex + k) % std::ssize(rFleet.members);
		if (rFleet.members.at(static_cast<size_t>(iCandidate)).bAlive)
		{
			iNewFlagship = iCandidate;
			break;
		}
	}
	if (iNewFlagship < 0)
	{
		return;
	}

	rFleet.iFlagshipIndex = iNewFlagship;
	engine::global_id_t newFlagshipId = rFleet.members.at(static_cast<size_t>(iNewFlagship)).globalPlayerId;
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}
	std::vector<engine::global_id_t>& rOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(iClientId).first->second;
	for (int64_t iOwned = 0; iOwned < std::ssize(rOwnedIds); ++iOwned)
	{
		if (rOwnedIds.at(iOwned) == newFlagshipId)
		{
			mPendingFlagshipUpdates.push_back({iClientId, iFleetIndex, pClient->authorizedCoords.at(iOwned)});
			break;
		}
	}
}

void ServerFleetManager::OnPlayerDeath(int64_t iClientId, engine::global_id_t globalId)
{
	auto fleetIt = mClientFleets.find(iClientId);
	if (fleetIt == mClientFleets.end())
	{
		return;
	}

	for (int64_t iFleet = 0; iFleet < std::ssize(fleetIt->second); ++iFleet)
	{
		Fleet& rFleet = fleetIt->second.at(static_cast<size_t>(iFleet));
		for (int64_t j = 0; j < std::ssize(rFleet.members); ++j)
		{
			if (rFleet.members.at(j).globalPlayerId == globalId && rFleet.members.at(j).bAlive)
			{
				rFleet.members.at(j).bAlive = false;

				if (j == rFleet.iFlagshipIndex)
				{
					ShiftFlagshipAfterDeath(iClientId, iFleet, rFleet);
				}

				SendFleetSyncToClient(iClientId);
				return;
			}
		}
	}
}

void ServerFleetManager::OnPlayerSpawned(int64_t iClientId, const ClientSpawnInfo& rSpawnInfo, engine::global_id_t globalPlayerId)
{
	if (rSpawnInfo.iFleetIndex < 0)
	{
		return;
	}

	std::vector<Fleet>& rFleets = mClientFleets.try_emplace(iClientId).first->second;
	if (rSpawnInfo.iFleetIndex >= std::ssize(rFleets))
	{
		return;
	}

	Fleet& rFleet = rFleets.at(static_cast<size_t>(rSpawnInfo.iFleetIndex));
	if (rSpawnInfo.iMemberIndex >= 0 && rSpawnInfo.iMemberIndex < std::ssize(rFleet.members))
	{
		// Respawn: replace dead member
		rFleet.members.at(static_cast<size_t>(rSpawnInfo.iMemberIndex)) = FleetMember {globalPlayerId, true};
	}
	else
	{
		// New member
		rFleet.members.push_back(FleetMember {globalPlayerId, true});
	}
	SendFleetSyncToClient(iClientId);

	// Queue flagship update if this member is or becomes the Flagship
	int64_t iThisMemberIndex = (rSpawnInfo.iMemberIndex >= 0)
		? rSpawnInfo.iMemberIndex
		: std::ssize(rFleet.members) - 1;
	bool bHasAliveFlagship = rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
		rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive &&
		iThisMemberIndex != rFleet.iFlagshipIndex;
	if (!bHasAliveFlagship)
	{
		rFleet.iFlagshipIndex = iThisMemberIndex;
		mPendingFlagshipUpdates.push_back({iClientId, rSpawnInfo.iFleetIndex, engine::kOriginCoord});
	}
}

void ServerFleetManager::OnPlayerTransferred(int64_t iClientId, engine::global_id_t globalPlayerId, engine::GridCoord destination)
{
	auto fleetIt = mClientFleets.find(iClientId);
	if (fleetIt == mClientFleets.end())
	{
		return;
	}

	for (int64_t iFleet = 0; iFleet < std::ssize(fleetIt->second); ++iFleet)
	{
		const Fleet& rFleet = fleetIt->second.at(static_cast<size_t>(iFleet));
		if (rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
			rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).globalPlayerId == globalPlayerId)
		{
			mPendingFlagshipUpdates.push_back({iClientId, iFleet, destination});
			break;
		}
	}
}

void ServerFleetManager::OnClientConnected(int64_t iClientId, const engine::ClientGuid& rClientGuid)
{
	const std::vector<engine::global_id_t>& rOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(iClientId).first->second;

	for (auto savedIt = mSavedFleets.begin(); savedIt != mSavedFleets.end(); ++savedIt)
	{
		if (savedIt->first == rClientGuid)
		{
			// Update alive flags based on re-linked players
			for (Fleet& rFleet : savedIt->second)
			{
				for (FleetMember& rMember : rFleet.members)
				{
					rMember.bAlive = std::ranges::contains(rOwnedIds, rMember.globalPlayerId);
				}
			}
			mClientFleets.insert_or_assign(iClientId, std::move(savedIt->second));
			mSavedFleets.erase(savedIt);
			break;
		}
	}
	SendFleetSyncToClient(iClientId);
}

void ServerFleetManager::OnClientDisconnected(int64_t iClientId, const engine::ClientGuid& rClientGuid)
{
	auto fleetIt = mClientFleets.find(iClientId);
	if (fleetIt != mClientFleets.end() && !fleetIt->second.empty())
	{
		if (!rClientGuid.IsEmpty())
		{
			std::erase_if(mSavedFleets, [&](const auto& rPair) { return rPair.first == rClientGuid; });
			mSavedFleets.push_back({rClientGuid, std::move(fleetIt->second)});
		}
		mClientFleets.erase(fleetIt);
	}
}

void ServerFleetManager::OnResetForLoad(int64_t iClientId, const engine::ClientGuid& rClientGuid)
{
	const std::vector<engine::global_id_t>& rOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(iClientId).first->second;
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);

	mClientFleets.erase(iClientId);
	if (!rClientGuid.IsEmpty())
	{
		for (auto& [rGuid, rFleets] : mSavedFleets)
		{
			if (rGuid == rClientGuid)
			{
				// Update alive flags and validate flagship index
				for (int64_t iFleet = 0; iFleet < std::ssize(rFleets); ++iFleet)
				{
					Fleet& rFleet = rFleets.at(static_cast<size_t>(iFleet));
					for (FleetMember& rMember : rFleet.members)
					{
						rMember.bAlive = std::ranges::contains(rOwnedIds, rMember.globalPlayerId);
					}

					// Shift flagship to next alive member if current flagship is dead
					if (rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
						!rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive)
					{
						ShiftFlagshipAfterDeath(iClientId, iFleet, rFleet);
					}
					else if (rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
						rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive)
					{
						// Flagship still alive — queue coord update
						engine::global_id_t flagshipId = rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).globalPlayerId;
						if (pClient != nullptr)
						{
							for (int64_t k = 0; k < std::ssize(rOwnedIds); ++k)
							{
								if (rOwnedIds.at(k) == flagshipId)
								{
									mPendingFlagshipUpdates.push_back({iClientId, iFleet, pClient->authorizedCoords.at(k)});
									break;
								}
							}
						}
					}
				}
				mClientFleets.insert_or_assign(iClientId, rFleets);
				break;
			}
		}
	}
	SendFleetSyncToClient(iClientId);
}

ServerFleetManager::FlagshipLookupResult ServerFleetManager::LookupFlagshipCoord(int64_t iClientId, int64_t iFleetIndex, int64_t iMemberIndex, engine::GridCoord spawnCoord)
{
	auto fleetIt = mClientFleets.find(iClientId);
	if (fleetIt == mClientFleets.end() || iFleetIndex >= std::ssize(fleetIt->second))
	{
		return {};
	}

	const Fleet& rFleet = fleetIt->second.at(static_cast<size_t>(iFleetIndex));
	if (iMemberIndex == rFleet.iFlagshipIndex ||
		(iMemberIndex < 0 && rFleet.members.empty()))
	{
		return {true, {}};
	}

	if (rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
		rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive)
	{
		// Flagship alive — find its current coord
		engine::global_id_t flagshipGlobalId = rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).globalPlayerId;
		engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
		if (pClient != nullptr)
		{
			std::vector<engine::global_id_t>& rOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(pClient->iClientId).first->second;
			for (int64_t k = 0; k < std::ssize(rOwnedIds); ++k)
			{
				if (rOwnedIds.at(k) == flagshipGlobalId)
				{
					return {false, pClient->authorizedCoords.at(k)};
				}
			}
		}
		return {};
	}

	// Flagship dead — set to spawn coord so no navigation override triggers
	return {false, spawnCoord};
}

void ServerFleetManager::WriteFleetData(std::fstream& rFileStream) const
{
	auto writeFleets = [&rFileStream](const engine::ClientGuid& rGuid, const std::vector<Fleet>& rFleets)
	{
		common::Write(rFileStream, rGuid.uiHigh);
		common::Write(rFileStream, rGuid.uiLow);
		int64_t iFleetCount = std::ssize(rFleets);
		common::Write(rFileStream, iFleetCount);
		for (const Fleet& rFleet : rFleets)
		{
			int64_t iMemberCount = std::ssize(rFleet.members);
			common::Write(rFileStream, iMemberCount);
			common::Write(rFileStream, rFleet.iFlagshipIndex);
			for (const FleetMember& rMember : rFleet.members)
			{
				common::Write(rFileStream, rMember.globalPlayerId.iValue);
				uint8_t uiAlive = rMember.bAlive ? 1 : 0;
				common::Write(rFileStream, uiAlive);
			}
		}
	};

	const std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();
	int64_t iClientCount = std::ssize(mSavedFleets);
	for (const engine::ClientConnection& rClient : rClients)
	{
		auto fleetIt = mClientFleets.find(rClient.iClientId);
		if (fleetIt != mClientFleets.end() && !fleetIt->second.empty())
		{
			++iClientCount;
		}
	}
	common::Write(rFileStream, iClientCount);

	for (const engine::ClientConnection& rClient : rClients)
	{
		auto fleetIt = mClientFleets.find(rClient.iClientId);
		if (fleetIt == mClientFleets.end() || fleetIt->second.empty())
		{
			continue;
		}
		writeFleets(rClient.clientGuid, fleetIt->second);
	}

	for (const auto& [rGuid, rFleets] : mSavedFleets)
	{
		writeFleets(rGuid, rFleets);
	}
}

void ServerFleetManager::ReadFleetData(std::fstream& rFileStream)
{
	mClientFleets.clear();
	mSavedFleets.clear();
	int64_t iClientCount = 0;
	common::Read(rFileStream, iClientCount);
	for (int64_t i = 0; i < iClientCount; ++i)
	{
		uint64_t uiGuidHigh = 0;
		uint64_t uiGuidLow = 0;
		common::Read(rFileStream, uiGuidHigh);
		common::Read(rFileStream, uiGuidLow);
		engine::ClientGuid guid {uiGuidHigh, uiGuidLow};
		int64_t iFleetCount = 0;
		common::Read(rFileStream, iFleetCount);
		std::vector<Fleet> fleets(static_cast<size_t>(iFleetCount));
		for (int64_t j = 0; j < iFleetCount; ++j)
		{
			int64_t iMemberCount = 0;
			common::Read(rFileStream, iMemberCount);
			common::Read(rFileStream, fleets.at(static_cast<size_t>(j)).iFlagshipIndex);
			fleets.at(static_cast<size_t>(j)).members.resize(static_cast<size_t>(iMemberCount));
			for (int64_t k = 0; k < iMemberCount; ++k)
			{
				int64_t iGlobalPlayerId = 0;
				common::Read(rFileStream, iGlobalPlayerId);
				uint8_t uiAlive = 0;
				common::Read(rFileStream, uiAlive);
				fleets.at(static_cast<size_t>(j)).members.at(static_cast<size_t>(k)) = FleetMember {engine::global_id_t {iGlobalPlayerId}, uiAlive != 0};
			}
		}
		mSavedFleets.push_back({guid, std::move(fleets)});
	}
}

void ServerFleetManager::ResetState()
{
	mPendingCreateFleetRequests.clear();
	mPendingSpawnIntoFleetRequests.clear();
	mPendingRespawnInFleetRequests.clear();
	mSavedFleets.clear();
}

#endif // BT_SERVER

} // namespace game
