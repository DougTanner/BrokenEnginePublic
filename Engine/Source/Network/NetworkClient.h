#pragma once

#include "Frame/Collections/Collection.h"
#include "Network/NetworkManager.h"
#include "Network/NetworkProtocol.h"

namespace game
{

struct PlayersInterpolate;
using player_t = engine::id_t<PlayersInterpolate>;

struct Frame;
struct StatusChange;
struct PlayerInput;

} // namespace game

namespace engine
{

struct ReceivedGridUpdate
{
	GridCoord coord {};
	common::crc_t serverCrc = 0;
	// Heap: ENet packet data, variable per frame
	std::vector<game::StatusChange> statusChanges;
};

struct ReceivedUpdate
{
	int64_t iFrame = 0;
	// Heap: variable number of grid cells per update
	std::vector<ReceivedGridUpdate> gridUpdates;
};

struct ReceivedFullState
{
	int64_t iFrame = 0;
	GridCoord coord {};
	std::unique_ptr<game::Frame> pFrame;
};

class NetworkClient
{
public:

	NetworkClient(const char* pServerAddress, uint16_t uiPort);
	~NetworkClient();

	void Poll();

	void SendInput(uint16_t uiPlayerId, const game::PlayerInput& rInput, bool bGamepad, float fRotateEye);
	void SendSpawnRequest(ClientRequestFlags_t flags);
	void SendDesyncReport(int64_t iFrame, GridCoord coord, common::crc_t expected, common::crc_t actual);

	std::vector<ReceivedUpdate>& DrainReceivedUpdates() { return mReceivedUpdates; }
	std::vector<ReceivedFullState>& DrainReceivedFullStates() { return mReceivedFullStates; }

	bool IsConnected() const { return mbConnected; }
	game::player_t GetAssignedPlayerId() const { return mAssignedPlayerId; }
	GridCoord GetAssignedGridCoord() const { return mAssignedGridCoord; }

private:

	void HandleReceive(ENetEvent& rEvent);
	void HandleServerAssignPlayer(const uint8_t* pData, size_t iSize);
	void HandleServerFullState(const uint8_t* pData, size_t iSize);
	void HandleServerUpdateStream(const uint8_t* pData, size_t iSize);

	void TrackReceivedFrame(int64_t iFrame);

	ENetHost* mpHost = nullptr;
	ENetPeer* mpServerPeer = nullptr;
	bool mbConnected = false;

	game::player_t mAssignedPlayerId {};
	GridCoord mAssignedGridCoord {};

	std::vector<ReceivedUpdate> mReceivedUpdates;
	std::vector<ReceivedFullState> mReceivedFullStates;

	// Frame tracking for re-send requests
	int64_t miHighestReceivedFrame = -1;
	std::vector<int64_t> mMissingFrames;
};

inline NetworkClient* gpNetworkClient = nullptr;

} // namespace engine
