#pragma once

#if defined(BT_SERVER)

#include "Network/Server/ServerSessionBase.h"

namespace game
{

struct ClientSpawnInfo
{
	int64_t iClientId = 0;
	engine::GridCoord spawnCoord {};
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
	player_t newPlayerId {};
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
	void ProcessWeaponModeRequests();
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

private:

	// ComputeActiveSet helpers
	void AddSubscribedCoords();
	void AddNeighborCoords();
	void EnsureSpecialCoords();
	void SyncActiveFrames();

	// HarvestTransfers helpers
	void CollectTransfers(std::vector<struct HumanTransferInfo>& rHumanTransfers);
	void SortTransfersByType();
	void SpawnTransfers();
	void TrackHumanTransfers(const std::vector<struct HumanTransferInfo>& rHumanTransfers);

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
