#include "Pch.h"

#include "Network/Server/ServerFleetManagerUtils.h"

#include "Network/GamePacketType.h"

namespace game
{

#if defined(BT_SERVER)

void SendFleetSync(int64_t iClientId, const std::vector<Fleet>& rFleets)
{
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kNetwork, kVerbose, "SendFleetSync Client: {} Fleets: {}", iClientId, rFleets.size());

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	// [1B type][8B fleetCount] per fleet: [8B guid.uiHigh][8B guid.uiLow][8B memberCount][8B iFlagshipIndex][4B navigationDelay] per member: [8B globalPlayerId][1B bAlive]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kServerFleetSync));
	rWorkbuffer.PushBack<int64_t>(std::ssize(rFleets));
	for (const Fleet& rFleet : rFleets)
	{
		rWorkbuffer.PushBack<uint64_t>(rFleet.guid.uiHigh);
		rWorkbuffer.PushBack<uint64_t>(rFleet.guid.uiLow);
		rWorkbuffer.PushBack<int64_t>(std::ssize(rFleet.members));
		rWorkbuffer.PushBack<int64_t>(rFleet.iFlagshipIndex);
		rWorkbuffer.PushBack<float>(rFleet.fNavigationDelay);
		for (const FleetMember& rMember : rFleet.members)
		{
			rWorkbuffer.PushBack<int64_t>(rMember.globalPlayerId.iValue);
			rWorkbuffer.PushBack<uint8_t>(rMember.bAlive ? 1 : 0);
		}
	}

	engine::NetworkManager::SendPacket(pClient->pPeer, engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

static void WriteFleet(std::fstream& rFileStream, const Fleet& rFleet)
{
	common::Write(rFileStream, rFleet.guid.uiHigh);
	common::Write(rFileStream, rFleet.guid.uiLow);
	int64_t iMemberCount = std::ssize(rFleet.members);
	common::Write(rFileStream, iMemberCount);
	common::Write(rFileStream, rFleet.iFlagshipIndex);
	common::Write(rFileStream, rFleet.wantedCoord.x);
	common::Write(rFileStream, rFleet.wantedCoord.y);
	common::Write(rFileStream, rFleet.uiPendingFleetWantedCoordTicks);
	common::Write(rFileStream, rFleet.fNavigationDelay);
	common::Write(rFileStream, rFleet.fFrameChangeTimer);
	for (const FleetMember& rMember : rFleet.members)
	{
		common::Write(rFileStream, rMember.globalPlayerId.iValue);
		uint8_t uiAlive = rMember.bAlive ? 1 : 0;
		common::Write(rFileStream, uiAlive);
		common::Write(rFileStream, rMember.coord.x);
		common::Write(rFileStream, rMember.coord.y);
	}
}

static void ReadFleet(std::fstream& rFileStream, Fleet& rFleet, std::unordered_map<engine::global_id_t, engine::ClientGuid, engine::GlobalIdHash>& rPlayerToGuid, const engine::ClientGuid& rGuid)
{
	common::Read(rFileStream, rFleet.guid.uiHigh);
	common::Read(rFileStream, rFleet.guid.uiLow);
	int64_t iMemberCount = 0;
	common::Read(rFileStream, iMemberCount);
	common::Read(rFileStream, rFleet.iFlagshipIndex);
	int32_t iWantedX = 0;
	int32_t iWantedY = 0;
	common::Read(rFileStream, iWantedX);
	common::Read(rFileStream, iWantedY);
	rFleet.wantedCoord = engine::GridCoord {iWantedX, iWantedY};
	common::Read(rFileStream, rFleet.uiPendingFleetWantedCoordTicks);
	common::Read(rFileStream, rFleet.fNavigationDelay);
	common::Read(rFileStream, rFleet.fFrameChangeTimer);
	// Trust boundary (save / replay file): bound the member count against the stream before resize.
	common::ValidateDeserializedCount(iMemberCount, sizeof(int64_t) + sizeof(uint8_t) + 2 * sizeof(int32_t), rFileStream, "ReadFleet members");
	rFleet.members.resize(static_cast<size_t>(iMemberCount));
	for (int64_t k = 0; k < iMemberCount; ++k)
	{
		int64_t iGlobalPlayerId = 0;
		common::Read(rFileStream, iGlobalPlayerId);
		uint8_t uiAlive = 0;
		common::Read(rFileStream, uiAlive);
		int32_t iCoordX = 0;
		int32_t iCoordY = 0;
		common::Read(rFileStream, iCoordX);
		common::Read(rFileStream, iCoordY);
		rFleet.members.at(static_cast<size_t>(k)) = FleetMember {engine::global_id_t {iGlobalPlayerId}, uiAlive != 0, engine::GridCoord {iCoordX, iCoordY}};

		// Rebuild reverse lookup
		if (uiAlive != 0)
		{
			rPlayerToGuid.insert_or_assign(engine::global_id_t {iGlobalPlayerId}, rGuid);
		}
	}
}

void WriteFleetData(std::fstream& rFileStream, const std::unordered_map<engine::ClientGuid, std::vector<Fleet>, engine::ClientGuidHash>& rFleets, const common::RandomEngine& rRandom)
{
	int64_t iFleetOwnerCount = std::ssize(rFleets);
	common::Write(rFileStream, iFleetOwnerCount);

	for (const auto& [rGuid, rFleetVec] : rFleets)
	{
		common::Write(rFileStream, rGuid.uiHigh);
		common::Write(rFileStream, rGuid.uiLow);
		int64_t iFleetCount = std::ssize(rFleetVec);
		common::Write(rFileStream, iFleetCount);
		for (const Fleet& rFleet : rFleetVec)
		{
			WriteFleet(rFileStream, rFleet);
		}
	}

	common::Write(rFileStream, rRandom.uiState);
}

void ReadFleetData(std::fstream& rFileStream, std::unordered_map<engine::ClientGuid, std::vector<Fleet>, engine::ClientGuidHash>& rFleets, std::unordered_map<engine::global_id_t, engine::ClientGuid, engine::GlobalIdHash>& rPlayerToGuid, std::unordered_map<engine::ClientGuid, int64_t, engine::ClientGuidHash>& rGuidToClientId, common::RandomEngine& rRandom)
{
	rFleets.clear();
	rPlayerToGuid.clear();
	rGuidToClientId.clear();

	int64_t iFleetOwnerCount = 0;
	common::Read(rFileStream, iFleetOwnerCount);
	// Trust boundary (save / replay file): bound the owner count against the stream (each owner
	// serializes at least its 16-byte GUID plus an int64 fleet count).
	common::ValidateDeserializedCount(iFleetOwnerCount, 2 * sizeof(uint64_t) + sizeof(int64_t), rFileStream, "ReadFleetData owners");
	for (int64_t i = 0; i < iFleetOwnerCount; ++i)
	{
		uint64_t uiGuidHigh = 0;
		uint64_t uiGuidLow = 0;
		common::Read(rFileStream, uiGuidHigh);
		common::Read(rFileStream, uiGuidLow);
		engine::ClientGuid guid {uiGuidHigh, uiGuidLow};
		int64_t iFleetCount = 0;
		common::Read(rFileStream, iFleetCount);
		// Trust boundary (save / replay file): bound the fleet count before constructing the vector
		// (each fleet serializes at least its 16-byte GUID).
		common::ValidateDeserializedCount(iFleetCount, 2 * sizeof(uint64_t), rFileStream, "ReadFleetData fleets");
		std::vector<Fleet> fleets(static_cast<size_t>(iFleetCount));
		for (int64_t j = 0; j < iFleetCount; ++j)
		{
			ReadFleet(rFileStream, fleets.at(static_cast<size_t>(j)), rPlayerToGuid, guid);
		}
		rFleets.insert_or_assign(guid, std::move(fleets));
		// All loaded fleets start as disconnected
		rGuidToClientId.insert_or_assign(guid, static_cast<int64_t>(0));
	}

	common::Read(rFileStream, rRandom.uiState);
}

#endif // BT_SERVER

} // namespace game
