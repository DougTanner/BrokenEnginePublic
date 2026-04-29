#pragma once

#if defined(BT_SERVER)

#include "Fleet.h"

namespace game
{

struct ClientSpawnInfo;

struct PendingFlagshipUpdate
{
	engine::ClientGuid clientGuid {};
	int64_t iFleetIndex = 0;
	engine::GridCoord newWantedCoord {};
	uint8_t uiPendingFleetWantedCoordTicks = 0;
};

struct PendingCreateFleetRequest
{
	int64_t iClientId = 0;
};

struct PendingDeleteFleetRequest
{
	int64_t iClientId = 0;
	int64_t iFleetIndex = 0;
};

struct PendingSpawnIntoFleetRequest
{
	int64_t iClientId = 0;
	int64_t iFleetIndex = 0;
};

struct PendingRespawnInFleetRequest
{
	int64_t iClientId = 0;
	int64_t iFleetIndex = 0;
	int64_t iMemberIndex = 0;
};

class ServerFleetManager
{
public:

	ServerFleetManager();

	void ProcessCreateFleetRequests();
	void ProcessDeleteFleetRequests();
	void ProcessSpawnIntoFleetRequests();
	void ProcessRespawnInFleetRequests();
	void TickFleetTimers();
	void ProcessFlagshipUpdates();

	void SendFleetSyncToClient(int64_t iClientId);
	void SendFleetSync(int64_t iClientId, const std::vector<Fleet>& rFleets);

	void QueueCreateRequest(const PendingCreateFleetRequest& rRequest);
	void QueueDeleteRequest(const PendingDeleteFleetRequest& rRequest);
	void QueueSpawnIntoRequest(const PendingSpawnIntoFleetRequest& rRequest);
	void QueueRespawnRequest(const PendingRespawnInFleetRequest& rRequest);
	void ClearPendingRequests();

	void OnPlayerDeath(const engine::ClientGuid& rGuid, engine::global_id_t globalId);
	void OnPlayerSpawned(int64_t iClientId, const ClientSpawnInfo& rSpawnInfo, engine::global_id_t globalPlayerId);
	void OnPlayerTransferred(const engine::ClientGuid& rGuid, engine::global_id_t globalPlayerId, engine::GridCoord destination);
	void OnClientConnected(int64_t iClientId, const engine::ClientGuid& rClientGuid);
	void OnClientDisconnected(int64_t iClientId, const engine::ClientGuid& rClientGuid);
	void OnResetForLoad(int64_t iClientId, const engine::ClientGuid& rClientGuid);

	struct FleetLookupResult
	{
		bool bIsFlagship = false;
		engine::GridCoord fleetWantedCoord {};
		uint8_t uiPendingFleetWantedCoordTicks = 0;
	};
	FleetLookupResult LookupFleetWantedCoord(int64_t iClientId, int64_t iFleetIndex, int64_t iMemberIndex, engine::GridCoord spawnCoord);

	void UpdateFleetNavigationDelay(const engine::ClientGuid& rGuid, int64_t iFleetIndex, float fDelay);

	void WriteFleetData(std::fstream& rFileStream) const;
	void ReadFleetData(std::fstream& rFileStream);

	void DetectDisconnectedPlayerDeaths();

	void ResetState();

	engine::ClientGuid FindGuidForClient(int64_t iClientId) const;
	int64_t FindClientIdForGuid(const engine::ClientGuid& rGuid) const;

	// All fleets keyed by persistent ClientGuid (survives disconnect/reconnect)
	std::unordered_map<engine::ClientGuid, std::vector<Fleet>, engine::ClientGuidHash> mFleets;

	// Reverse lookup: find fleet owner for any player
	std::unordered_map<engine::global_id_t, engine::ClientGuid, engine::GlobalIdHash> mPlayerToGuid;

	// Connected client mapping: ClientGuid -> iClientId (0 = disconnected)
	std::unordered_map<engine::ClientGuid, int64_t, engine::ClientGuidHash> mGuidToClientId;

	std::vector<PendingFlagshipUpdate> mPendingFlagshipUpdates;

	common::RandomEngine mRandomEngine;

private:

	void ShiftFlagshipAfterDeath(const engine::ClientGuid& rGuid, int64_t iFleetIndex, Fleet& rFleet);

	std::vector<PendingCreateFleetRequest> mPendingCreateFleetRequests;
	std::vector<PendingDeleteFleetRequest> mPendingDeleteFleetRequests;
	std::vector<PendingSpawnIntoFleetRequest> mPendingSpawnIntoFleetRequests;
	std::vector<PendingRespawnInFleetRequest> mPendingRespawnInFleetRequests;
};

} // namespace game

#endif // BT_SERVER
