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

struct ClientConnection
{
	ENetPeer* pPeer = nullptr;
	int64_t iClientId = 0;
	uint16_t uiPlayerId = 0;
	game::player_t humanPlayerId {};
	GridCoord humanGridCoord {};

	// Active set (3x3 grid around player)
	std::vector<GridCoord> activeCoords;

	// For server-side press detection
	game::FrameInputHeldFlags_t previousHeldFlags {};

	// ACK state received from client for proactive re-sends
	int64_t iAckFloor = -1;
	uint64_t uiReceivedBitfield = 0;

	// Pipeline RTT: echoed back to client in update packets
	int64_t iClientTimestampNs = 0;

	// Coords needing full state after subscription change (populated by SendAssignPlayer)
	std::vector<GridCoord> pendingFullStateCoords;
};

struct PendingInput
{
	int64_t iClientId = 0;
	game::FrameInputHeldFlags_t heldFlags {};
	XMFLOAT3 f3Move {};
	XMVECTOR vecDirection {};
	bool bGamepad = false;
	float fRotateEye = 0.0f;

	// Derived by server from held state delta
	game::FrameInputPressedFlags_t pressedFlags {};
};

struct PendingSpawnRequest
{
	int64_t iClientId = 0;
	ClientRequestFlags_t flags {};
};

struct PendingDisconnect
{
	int64_t iClientId = 0;
	game::player_t playerId {};
	GridCoord coord {};
};

struct GridUpdateData
{
	common::crc_t serverCrc = 0;
	std::span<const game::StatusChange> statusChanges;
	std::span<const game::PlayerInput> playerInputs;
};

// Ring buffer entry for re-send support
struct BufferedGridData
{
	GridCoord coord {};
	common::crc_t serverCrc = 0;
	// Heap: variable-size compressed status change data per grid cell per frame
	std::vector<uint8_t> compressedData;
	// Heap: player inputs for re-send support
	std::vector<game::PlayerInput> playerInputs;
};

struct BufferedFrame
{
	int64_t iFrame = 0;
	// Heap: variable number of grid cells per frame
	std::vector<BufferedGridData> gridData;
};

struct BufferedFullFrame
{
	int64_t iFrame = 0;
	// Heap: serialized frame data per grid coordinate for debug frame requests
	std::unordered_map<GridCoord, std::string> serializedFrames;
};

class NetworkServer
{
public:

	NetworkServer(uint16_t uiPort);
	~NetworkServer();

	void Poll();

	void SendAssignPlayer(int64_t iClientId, game::player_t playerId, GridCoord coord);
	void SendFullState(int64_t iClientId, int64_t iFrame, const std::vector<std::pair<GridCoord, const game::Frame*>>& rFrames);
	void BufferFrame(int64_t iFrame, const std::vector<std::pair<GridCoord, GridUpdateData>>& rGridUpdates);
	void BufferFullFrame(int64_t iFrame, const std::vector<std::pair<GridCoord, const game::Frame*>>& rFrames);
	void SendUpdate(ClientConnection& rClient, int64_t iFrame, const std::vector<std::pair<GridCoord, GridUpdateData>>& rGridUpdates);
	void SendResends(ClientConnection& rClient, int64_t iFrame);
	void Flush();
	std::vector<PendingInput>& DrainPendingInputs() { return mPendingInputs; }
	std::vector<PendingSpawnRequest>& DrainPendingSpawnRequests() { return mPendingSpawnRequests; }
	std::vector<PendingDisconnect>& DrainPendingDisconnects() { return mPendingDisconnects; }
	const std::vector<ClientConnection>& GetClients() const { return mClients; }
	std::vector<ClientConnection>& GetClients() { return mClients; }

private:

	void HandleConnect(ENetEvent& rEvent);
	void HandleDisconnect(ENetEvent& rEvent);
	void HandleReceive(ENetEvent& rEvent);

	void HandleClientInputStream(const uint8_t* pData, size_t iSize, int64_t iClientId);
	void HandleClientSpawnRequest(const uint8_t* pData, size_t iSize, int64_t iClientId);
	void HandleClientDesyncReport(const uint8_t* pData, size_t iSize);
	void HandleClientDebugFrameRequest(const uint8_t* pData, size_t iSize, ENetPeer* pPeer);

	ClientConnection* FindClient(int64_t iClientId);

	ENetHost* mpHost = nullptr;
	std::vector<ClientConnection> mClients;
	std::vector<PendingInput> mPendingInputs;
	std::vector<PendingSpawnRequest> mPendingSpawnRequests;
	std::vector<PendingDisconnect> mPendingDisconnects;
	int64_t miNextClientId = 1;

	// Ring buffer for re-sends
	std::deque<BufferedFrame> mBufferedFrames;

	// Ring buffer for debug frame requests
	std::deque<BufferedFullFrame> mBufferedFullFrames;
};

inline NetworkServer* gpNetworkServer = nullptr;

} // namespace engine
