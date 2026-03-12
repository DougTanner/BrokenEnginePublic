#pragma once

#if defined(BT_SERVER)

#include "Network/NetworkServer/ServerSessionBase.h"

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
	void ComputeActiveSet();
	void BuildFrameInputs();
	void ProcessSpawnRequests();
	void HarvestTransfers();
	void BroadcastStatusChanges(int64_t iTick);
	void HandleDisconnects();
	void HandleNewClients();
	void FinalizeNewClients(int64_t iTick);
	void DetectPlayerDeaths();
	void HandleSubscriptionUpdates(int64_t iTick);
	void RefreshPreSpawnSnapshot();

private:

	std::vector<PendingPlayerDestroy> mPendingPlayerDestroys;
	std::vector<ClientSpawnInfo> mClientsWaitingForSpawn;
	std::unordered_set<int64_t> mDeadClientIds;
	std::vector<player_t> mPreSpawnPlayerIds;
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mBroadcastSpawns;
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> mBroadcastTransfers;
	std::vector<SubscriptionUpdate> mPendingSubscriptionUpdates;
};

inline ServerSession* gpServerSession = nullptr;

} // namespace game

#endif // BT_SERVER
