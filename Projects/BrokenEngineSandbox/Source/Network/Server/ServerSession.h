#pragma once

#if defined(BT_SERVER)

#include "Network/Server/ServerSessionBase.h"

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
	void ParseReceivedGamePackets();
	void SendAssignPlayer(int64_t iClientId, engine::global_id_t globalId, engine::GridCoord coord);
	void SendPlayerState(int64_t iClientId, uint8_t uiStateType, int64_t iGlobalPlayerId, engine::GridCoord coord);
	void SubscriptionUpdates(int64_t iTick);
	void HandleResyncRequests(int64_t iTick);
	void ResetClientsForLoad();
	void WriteFleetData(std::fstream& rFileStream) const;
	void ReadFleetData(std::fstream& rFileStream);

	std::unordered_map<int64_t, std::vector<engine::global_id_t>> mClientOwnedPlayerIds;

	std::unique_ptr<ServerFleetManager> mpFleetManager;
	std::unique_ptr<ServerTransferManager> mpTransferManager;
	std::unique_ptr<ServerBroadcaster> mpBroadcaster;
	std::unique_ptr<ServerClientManager> mpClientManager;

private:

	// ComputeActiveSet helpers
	void AddSubscribedCoords();
	void EnsurePlayerCoords();
	void EnsureDestroyCoords();
	void SyncActiveFrames();
};

inline ServerSession* gpServerSession = nullptr;

} // namespace game

#endif // BT_SERVER
