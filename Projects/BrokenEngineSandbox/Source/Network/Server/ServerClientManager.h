#pragma once

#if defined(BT_SERVER)

namespace game
{

struct ClientSpawnInfo
{
	int64_t iClientId = 0;
	engine::GridCoord spawnCoord {};
	int64_t iFleetIndex = -1;
	int64_t iMemberIndex = -1;
};

struct PendingPlayerDestroy
{
	engine::GridCoord coord {};
	player_t playerId {};
};

class ServerClientManager
{
public:

	void QueueSpawnForClient(int64_t iClientId, engine::GridCoord spawnCoord, int64_t iFleetIndex = -1, int64_t iMemberIndex = -1);
	void ProcessSpawnRequests();
	void NewClients();
	void FinalizeNewClients(int64_t iTick);
	void Disconnects();
	void DetectPlayerDeaths();
	void RefreshPreSpawnSnapshot();
	void ResetState();

	std::vector<ClientSpawnInfo> mClientsWaitingForSpawn;
	std::unordered_set<int64_t> mDeadClientIds;
	std::unordered_set<int64_t> mProcessedClientIds;
	std::vector<player_t> mPreSpawnPlayerIds;
	std::vector<PendingPlayerDestroy> mPendingPlayerDestroys;

private:

	// NewClients helpers
	void LogConnectingClientDiagnostic(const engine::ClientConnection& rClient);
	bool TryRelinkNewClient(engine::ClientConnection& rClient, std::vector<engine::global_id_t>& rNewClientOwnedIds);
};

} // namespace game

#endif // BT_SERVER
