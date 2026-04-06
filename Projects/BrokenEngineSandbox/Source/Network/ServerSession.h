#pragma once

#if defined(BT_SERVER)

#include "Fleet.h"
#include "Network/Server/ServerSessionBase.h"

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

struct SubscriptionUpdate
{
	int64_t iClientId = 0;
	engine::GridCoord newCoord {};
	engine::global_player_t globalPlayerId {};
};

class ServerSession : public engine::ServerSessionBase
{
public:

	ServerSession();
	~ServerSession() override;

	void PreTickNetwork();
	void PrepareTick();
	void BroadcastTick(int64_t iTick);
	void SendResends(int64_t iTick);
	void WaitForTick(engine::TimeStep& rTimeStep);
	void ComputeActiveSet();
	void BuildFrameInputs();
	void ProcessSpawnRequests();
	void ProcessCreateFleetRequests();
	void ProcessSpawnIntoFleetRequests();
	void ProcessRespawnInFleetRequests();
	void SendFleetSyncToClient(int64_t iClientId);
	void ProcessUpdatePlayerRequests();
	void ProcessFlagshipUpdates();
	void HarvestTransfers();
	void BroadcastStatusChanges(int64_t iTick);
	void Disconnects();
	void NewClients();
	void FinalizeNewClients(int64_t iTick);
	void DetectPlayerDeaths();
	void SubscriptionUpdates(int64_t iTick);
	void HandleResyncRequests(int64_t iTick);
	void RefreshPreSpawnSnapshot();
	void ResetClientsForLoad();

	struct PendingFlagshipUpdate
	{
		int64_t iClientId = 0;
		int64_t iFleetIndex = 0;
		engine::GridCoord newFlagshipCoord {};
	};

	std::unordered_map<int64_t, std::vector<Fleet>> mClientFleets;
	std::vector<std::pair<engine::ClientGuid, std::vector<Fleet>>> mSavedFleets;
	std::vector<PendingFlagshipUpdate> mPendingFlagshipUpdates;

private:

	// ComputeActiveSet helpers
	void AddSubscribedCoords();
	void EnsurePlayerCoords();
	void EnsureDestroyCoords();
	void SyncActiveFrames();

	// HarvestTransfers helpers
	void CollectTransfers(std::vector<struct ClientTransferInfo>& rClientTransfers);
	void SortTransfersByType();
	void SpawnTransfers();
	void TrackClientTransfers(const std::vector<struct ClientTransferInfo>& rClientTransfers);

	std::vector<PendingPlayerDestroy> mPendingPlayerDestroys;
	std::vector<ClientSpawnInfo> mClientsWaitingForSpawn;
	std::unordered_set<int64_t> mDeadClientIds;
	std::vector<player_t> mPreSpawnPlayerIds;
	struct TickBroadcastData
	{
		std::unordered_map<engine::GridCoord, std::vector<StatusChange>> spawns;
		std::unordered_map<engine::GridCoord, std::vector<StatusChange>> transfers;
	};
	TickBroadcastData mTickBroadcast;
	std::vector<SubscriptionUpdate> mPendingSubscriptionUpdates;
};

inline ServerSession* gpServerSession = nullptr;

} // namespace game

#endif // BT_SERVER
