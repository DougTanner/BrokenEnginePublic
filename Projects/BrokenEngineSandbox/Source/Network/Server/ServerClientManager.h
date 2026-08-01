#pragma once

#if defined(BT_SERVER)

#include "Fleet.h"

namespace game
{

struct ClientSpawnInfo
{
	int64_t iClientId = 0;
	engine::ClientGuid clientGuid {};
	// Default-constructed (invalid) guid means this spawn is not fleet-triggered.
	FleetGuid fleetGuid {};
	int64_t iMemberIndex = -1;
};

class ServerClientManager
{
public:

	void QueueSpawnForClient(int64_t iClientId, const engine::ClientGuid& rClientGuid, const FleetGuid& rFleetGuid = {}, int64_t iMemberIndex = -1);
	void NewClients();
	void FinalizeNewClients();
	void Disconnects();
	void DetectPlayerDeaths();
	void RefreshPreSpawnSnapshot();
	void ResetState();

	std::vector<ClientSpawnInfo> mClientsWaitingForSpawn;
	std::unordered_set<int64_t> mDeadClientIds;
	std::unordered_set<int64_t> mProcessedClientIds;
	std::vector<player_t> mPreSpawnPlayerIds;

	// Tracks whether the engine client set was non-empty as of the previous Disconnects() call so the
	// unpause/timescale-reset fires on the non-empty -> empty transition (see Disconnects()).
	bool mbHadClients = false;

private:

	// NewClients helpers
	void LogConnectingClientDiagnostic(const engine::ClientConnection& rClient);
};

} // namespace game

#endif // BT_SERVER
