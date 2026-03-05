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

struct ClientCoordSubscription
{
	GridCoord coord {};
	bool bActive = false;
};

struct PerCoordAckState
{
	int64_t iAckFloor = -1;
	uint64_t uiReceivedBitfield = 0;
	uint16_t uiEpoch = 0;
};

struct ClientConnection
{
	ENetPeer* pPeer = nullptr;
	int64_t iClientId = 0;
	game::player_t humanPlayerId {};
	GridCoord humanGridCoord {};

	// Slot-based subscriptions (replaces activeCoords + pendingFullStateCoords)
	ClientCoordSubscription coordSubscriptions[NetworkManager::kiMaxCoordSlots] {};
	PerCoordAckState coordAckStates[NetworkManager::kiMaxCoordSlots] {};

	// For server-side press detection
	game::FrameInputHeldFlags_t previousHeldFlags {};

	// Input sequence for out-of-order protection
	uint32_t uiLatestInputSequence = 0;

	// Pipeline RTT: echoed back to client in update packets
	int64_t iClientTimestampNs = 0;

	// Helpers
	int64_t FindSlotForCoord(GridCoord coord) const
	{
		for (int64_t i = 0; i < NetworkManager::kiMaxCoordSlots; ++i)
		{
			if (coordSubscriptions[i].bActive && coordSubscriptions[i].coord == coord)
			{
				return i;
			}
		}
		return -1;
	}

	int64_t AllocateSlot()
	{
		for (int64_t i = 0; i < NetworkManager::kiMaxCoordSlots; ++i)
		{
			if (!coordSubscriptions[i].bActive)
			{
				return i;
			}
		}
		return -1;
	}

	void FreeSlot(int64_t iSlot)
	{
		coordSubscriptions[iSlot] = {};
		// Reset ACK state but preserve epoch (incremented on next allocation)
		coordAckStates[iSlot].iAckFloor = -1;
		coordAckStates[iSlot].uiReceivedBitfield = 0;
	}

	bool IsCoordSubscribed(GridCoord coord) const
	{
		return FindSlotForCoord(coord) >= 0;
	}
};

struct PendingInput
{
	int64_t iClientId = 0;
	game::FrameInputHeldFlags_t heldFlags {};
	XMFLOAT3 f3Move {};
	XMVECTOR vecDirection {};

	// Derived by server from held state delta
	game::FrameInputPressedFlags_t pressedFlags {};

	uint32_t uiInputSequence = 0;
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

struct PendingNewSubscription
{
	int64_t iClientId = 0;
	int64_t iSlot = 0;
	GridCoord coord {};
};

struct GridUpdateData
{
	common::crc_t serverCrc = 0;
	common::crc_t inputCrc = 0;
	std::span<const game::StatusChange> statusChanges;
	std::span<const game::PlayerInput> playerInputs;
};

// Per-coord ring buffer entry for re-send support
struct PerCoordBufferedFrame
{
	int64_t iFrame = 0;
	common::crc_t serverCrc = 0;
	common::crc_t inputCrc = 0;
	// Heap: variable-size compressed status change data per frame
	std::vector<uint8_t> compressedData;
	// Heap: player inputs for re-send support
	std::vector<game::PlayerInput> playerInputs;
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
	void SendPlayerState(int64_t iClientId, PlayerStateType eStateType, game::player_t playerId, GridCoord coord);
	void SendCoordFullState(int64_t iClientId, int64_t iSlot, int64_t iFrame, GridCoord coord, const game::Frame* pFrame);
	void BufferFrame(int64_t iFrame, const std::vector<std::pair<GridCoord, GridUpdateData>>& rGridUpdates);
	void BufferFullFrame(int64_t iFrame, const std::vector<std::pair<GridCoord, const game::Frame*>>& rFrames);
	void SendUpdate(ClientConnection& rClient, int64_t iFrame);
	void SendResends(ClientConnection& rClient, int64_t iFrame);
	void Flush();
	std::vector<PendingInput>& DrainPendingInputs() { return mPendingInputs; }
	std::vector<PendingSpawnRequest>& DrainPendingSpawnRequests() { return mPendingSpawnRequests; }
	std::vector<PendingDisconnect>& DrainPendingDisconnects() { return mPendingDisconnects; }
	std::vector<PendingNewSubscription>& DrainPendingNewSubscriptions() { return mPendingNewSubscriptions; }
	const std::vector<ClientConnection>& GetClients() const { return mClients; }
	std::vector<ClientConnection>& GetClients() { return mClients; }
	ClientConnection* FindClient(int64_t iClientId);
	const ClientConnection* FindClient(int64_t iClientId) const;

private:

	void HandleConnect(ENetEvent& rEvent);
	void HandleDisconnect(ENetEvent& rEvent);
	void HandleReceive(ENetEvent& rEvent);
	void HandleReceive(const uint8_t* pData, size_t iSize, ENetPeer* pPeer);

	void HandleClientInputStream(const uint8_t* pData, int64_t iClientId);
	void HandleClientSpawnRequest(const uint8_t* pData, int64_t iClientId);
	void HandleClientDesyncReport(const uint8_t* pData);
	void HandleClientDebugFrameRequest(const uint8_t* pData, ENetPeer* pPeer);
	void HandleClientHello(const uint8_t* pData, size_t iSize, ENetPeer* pPeer, int64_t iClientId);
	void HandleClientSubscribe(const uint8_t* pData, int64_t iClientId);
	void HandleClientUnsubscribe(const uint8_t* pData, int64_t iClientId);
	void SendConnectionResponse(ENetPeer* pPeer, bool bAccepted, const char* pMessage);
	void SendSubscribeAccept(ClientConnection& rClient, int64_t iSlot, GridCoord coord);
	void SendUnsubscribeAck(ClientConnection& rClient, int64_t iSlot);

	void WriteBufferedFramePacket(common::Workbuffer& rWorkbuffer, PacketType eType, int64_t iSlot, uint16_t uiEpoch, const PerCoordBufferedFrame& rBuffered, int64_t iTimestampNs);
	const PerCoordBufferedFrame* FindBufferedFrame(GridCoord coord, int64_t iFrame) const;
	int CompressToBuffer(const char* pData, int iSize);
	void RemoveClient(int64_t iClientId);

	ENetHost* mpHost = nullptr;
	std::vector<ClientConnection> mClients;
	std::vector<PendingInput> mPendingInputs;
	std::vector<PendingSpawnRequest> mPendingSpawnRequests;
	std::vector<PendingDisconnect> mPendingDisconnects;
	std::vector<PendingNewSubscription> mPendingNewSubscriptions;
	int64_t miNextClientId = 1;

	// Per-coord ring buffers for re-sends
	std::unordered_map<GridCoord, std::deque<PerCoordBufferedFrame>> mPerCoordBufferedFrames;

	// Ring buffer for debug frame requests
	std::deque<BufferedFullFrame> mBufferedFullFrames;

	// Compression scratch buffer (reused across BufferFrame calls)
	std::vector<uint8_t> mCompressionBuffer;

	// Network simulation delay queue
	std::deque<DelayedPacket> mDelayedPackets;
};

inline NetworkServer* gpNetworkServer = nullptr;

} // namespace engine
