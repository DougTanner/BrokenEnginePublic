#pragma once

#if defined(BT_SERVER)

#include "Fleet.h"

namespace game
{

struct ClientSpawnInfo;

struct PendingFlagshipUpdate
{
	int64_t iClientId = 0;
	int64_t iFleetIndex = 0;
	engine::GridCoord newFlagshipCoord {};
};

struct PendingCreateFleetRequest
{
	int64_t iClientId = 0;
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

	void ProcessCreateFleetRequests();
	void ProcessSpawnIntoFleetRequests();
	void ProcessRespawnInFleetRequests();
	void ProcessFlagshipUpdates();

	void SendFleetSyncToClient(int64_t iClientId);
	void SendFleetSync(int64_t iClientId, const std::vector<Fleet>& rFleets);

	void QueueCreateRequest(const PendingCreateFleetRequest& rRequest);
	void QueueSpawnIntoRequest(const PendingSpawnIntoFleetRequest& rRequest);
	void QueueRespawnRequest(const PendingRespawnInFleetRequest& rRequest);
	void ClearPendingRequests();

	void OnPlayerDeath(int64_t iClientId, engine::global_id_t globalId);
	void OnPlayerSpawned(int64_t iClientId, const ClientSpawnInfo& rSpawnInfo, engine::global_id_t globalPlayerId);
	void OnPlayerTransferred(int64_t iClientId, engine::global_id_t globalPlayerId, engine::GridCoord destination);
	void OnClientConnected(int64_t iClientId, const engine::ClientGuid& rClientGuid);
	void OnClientDisconnected(int64_t iClientId, const engine::ClientGuid& rClientGuid);
	void OnResetForLoad(int64_t iClientId, const engine::ClientGuid& rClientGuid);

	struct FlagshipLookupResult
	{
		bool bIsFlagship = false;
		engine::GridCoord flagshipCoord {};
	};
	FlagshipLookupResult LookupFlagshipCoord(int64_t iClientId, int64_t iFleetIndex, int64_t iMemberIndex, engine::GridCoord spawnCoord);

	void WriteFleetData(std::fstream& rFileStream) const;
	void ReadFleetData(std::fstream& rFileStream);

	void ResetState();

	std::unordered_map<int64_t, std::vector<Fleet>> mClientFleets;
	std::vector<std::pair<engine::ClientGuid, std::vector<Fleet>>> mSavedFleets;
	std::vector<PendingFlagshipUpdate> mPendingFlagshipUpdates;

private:

	void ShiftFlagshipAfterDeath(int64_t iClientId, int64_t iFleetIndex, Fleet& rFleet);

	std::vector<PendingCreateFleetRequest> mPendingCreateFleetRequests;
	std::vector<PendingSpawnIntoFleetRequest> mPendingSpawnIntoFleetRequests;
	std::vector<PendingRespawnInFleetRequest> mPendingRespawnInFleetRequests;
};

} // namespace game

#endif // BT_SERVER
