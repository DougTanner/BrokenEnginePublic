#pragma once

namespace game
{

struct PlayersInterpolate;
using player_t = engine::id_t<PlayersInterpolate>;

struct Frame;
struct StatusChange;

} // namespace game

namespace engine
{

struct ReceivedGridUpdate
{
	GridCoord coord {};
	common::crc_t serverCrc = 0;
	// Heap: ENet packet data, variable per frame
	std::vector<game::StatusChange> statusChanges;
	// Heap: server-echoed player inputs per grid cell
	std::vector<game::PlayerInput> playerInputs;
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

struct ReceivedDebugFrame
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
	void SendDebugFrameRequest(int64_t iFrame, GridCoord coord);
	void Flush();
	void Disconnect();

	std::vector<ReceivedUpdate>& DrainReceivedUpdates() { return mReceivedUpdates; }
	std::vector<ReceivedFullState>& DrainReceivedFullStates() { return mReceivedFullStates; }
	std::unique_ptr<ReceivedDebugFrame> DrainReceivedDebugFrame() { return std::move(mpReceivedDebugFrame); }

	int64_t GetAckFloor() const { return miAckFloor; }
	bool IsConnected() const { return mbConnected; }
	bool WasDisconnected() const { return mbDisconnectedEvent; }
	game::player_t GetAssignedPlayerId() const { return mAssignedPlayerId; }
	GridCoord GetAssignedGridCoord() const { return mAssignedGridCoord; }
	void ClearAssignment() { mAssignedPlayerId = {}; mAssignedGridCoord = {}; }

	uint64_t GetReceivedBitfield() const { return muiReceivedBitfield; }
	ENetPeer* GetServerPeer() const { return mpServerPeer; }
	int64_t GetBytesInPerSecond() { return mBytesInPerSecond.Get(); }
	int64_t GetBytesOutPerSecond() { return mBytesOutPerSecond.Get(); }
	int64_t GetPipelineRttUs() { return mSmoothedPipelineRttUs.Get(); }

private:

	void HandleReceive(ENetEvent& rEvent);
	void HandleServerAssignPlayer(const uint8_t* pData, size_t iSize);
	void HandleServerFullState(const uint8_t* pData, size_t iSize);
	void HandleServerUpdateStream(const uint8_t* pData, size_t iSize);
	void HandleServerResendStream(const uint8_t* pData, size_t iSize);
	void HandleServerDebugFrame(const uint8_t* pData, size_t iSize);

	void TrackReceivedFrame(int64_t iFrame);

	ENetHost* mpHost = nullptr;
	ENetPeer* mpServerPeer = nullptr;
	bool mbConnected = false;
	bool mbDisconnectedEvent = false;

	game::player_t mAssignedPlayerId {};
	GridCoord mAssignedGridCoord {};

	std::vector<ReceivedUpdate> mReceivedUpdates;
	std::vector<ReceivedFullState> mReceivedFullStates;

	std::unique_ptr<ReceivedDebugFrame> mpReceivedDebugFrame;

	// ACK tracking for proactive re-sends
	int64_t miAckFloor = -1;
	uint64_t muiReceivedBitfield = 0;

	// Pipeline RTT (timestamp echo)
	common::Smoothed<int64_t> mSmoothedPipelineRttUs;

	// Bandwidth tracking (host-level cumulative counters)
	uint32_t muiPrevReceivedData = 0;
	uint32_t muiPrevSentData = 0;
	common::InTheLastSecond mBytesInPerSecond;
	common::InTheLastSecond mBytesOutPerSecond;
};

inline NetworkClient* gpNetworkClient = nullptr;

} // namespace engine
