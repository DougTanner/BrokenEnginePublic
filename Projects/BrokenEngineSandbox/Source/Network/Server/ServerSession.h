#pragma once

#if defined(BT_SERVER)

#include "Network/PlayerEvents.h"
#include "Network/Server/ClientPlayerRegistry.h"

namespace engine
{

class NetworkDiscoveryResponder;
class ServerSessionRuntime;

} // namespace engine

namespace game
{

class ServerFleetManager;
class ServerTransferManager;
class ServerBroadcaster;
class ServerClientManager;

struct SubscriptionUpdate
{
	int64_t iClientId = 0;
	engine::GridCoord newCoord {};
	engine::global_id_t globalPlayerId {};
};

class ServerSession
{
public:

	ServerSession();
	~ServerSession();

	void PrepareTick();
	void ComputeActiveSet();
	void ParseReceivedGamePackets();
	void SendAssignPlayer(int64_t iClientId, engine::global_id_t globalId, engine::GridCoord coord);
	void SendPlayerState(int64_t iClientId, PlayerStateWireType eWireType, int64_t iGlobalPlayerId, engine::GridCoord coord);
	void BroadcastTimespeedIfChanged();
	void StepTimescale(bool bFaster); // step time scale one notch (faster/slower) and broadcast; shared by the packet handler and the agent command

	void SendTimespeedToNewClient(ENetPeer* pPeer);
	void SubscriptionUpdates();
	void SendNewSubscriptionFullStates();
	void HandleResyncRequests();
	void ResetClientsForLoad();
	void WriteFleetData(std::fstream& rFileStream) const;

	ClientPlayerRegistry mClientPlayers;

	std::unique_ptr<ServerFleetManager> mpFleetManager;
	std::unique_ptr<ServerTransferManager> mpTransferManager;
	std::unique_ptr<ServerBroadcaster> mpBroadcaster;
	std::unique_ptr<ServerClientManager> mpClientManager;
	std::unique_ptr<engine::ServerSessionRuntime> mpRuntime;

private:
	friend class engine::ServerSessionRuntime;

	void BeforeNetworkPoll();
	void AfterNetworkPoll();
	void FinalizeTickClients();
	void PreparePausedSubscriptions();

	// ComputeActiveSet helpers
	void AddSubscribedCoords();
	void EnsurePlayerCoords();
	void SyncActiveFrames();

};

inline ServerSession* gpServerSession = nullptr;

} // namespace game

#endif // BT_SERVER
