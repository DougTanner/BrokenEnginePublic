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

// Per-coord received update (single coord, not multi-coord)
struct ReceivedCoordUpdate
{
	int64_t iTick = 0;
	common::crc_t serverCrc = 0;
	common::crc_t inputCrc = 0;
	// Heap: ENet packet data, variable per frame
	std::vector<game::StatusChange> statusChanges;
};

// Per-coord received full state
struct ReceivedCoordFullState
{
	int64_t iTick = 0;
	GridCoord coord {};
	int64_t iSlot = -1;
	std::unique_ptr<game::Frame> pFrame;
};

enum class CoordSubscriptionState : uint8_t
{
	kUnsubscribed,
	kSubscribing,       // kClientSubscribe sent, waiting for accept
	kWaitingFullState,  // Accept received, waiting for full state
	kActive,            // Receiving delta updates
	kUnsubscribing,     // kClientUnsubscribe sent, waiting for ack
};

struct ClientCoordSlot
{
	GridCoord coord {};
	CoordSubscriptionState eState = CoordSubscriptionState::kUnsubscribed;

	// Per-slot ACK state
	int64_t iAckFloor = -1;
	uint64_t uiReceivedBitfield = 0;
	uint16_t uiEpoch = 0;
};

struct ReceivedAssignment
{
	game::player_t playerId {};
	GridCoord coord {};
};

struct ReceivedPlayerState
{
	PlayerStateType eType {};
	game::player_t playerId {};
	GridCoord coord {};
};

struct ReceivedDebugFrame
{
	int64_t iTick = 0;
	GridCoord coord {};
	std::unique_ptr<game::Frame> pFrame;
};

class ClientNetwork
{
public:

	ClientNetwork(const char* pServerAddress, uint16_t uiPort, int64_t iCoordSlots);
	~ClientNetwork();

	void Poll();

	void SendAck();
	void SendSpawnRequest(ClientRequestFlags_t flags);
	void SendDesyncReport(int64_t iTick, GridCoord coord, common::crc_t expected, common::crc_t actual);
	void SendDebugFrameRequest(int64_t iTick, GridCoord coord);
	void SendSubscribe(GridCoord coord);
	void SendUnsubscribe(int64_t iSlot);
	void Flush();
	void Disconnect();
	void SetDesyncDebugMode(bool bEnabled) { mbDesyncDebugMode = bEnabled; }

	std::vector<std::vector<ReceivedCoordUpdate>>& DrainReceivedCoordUpdates() { return mReceivedCoordUpdates; }
	std::vector<ReceivedCoordFullState>& DrainReceivedFullStates() { return mReceivedFullStates; }
	std::unique_ptr<ReceivedDebugFrame> DrainReceivedDebugFrame() { return std::move(mpReceivedDebugFrame); }

	bool IsConnected() const { return mbConnected; }
	bool IsConnectionAccepted() const { return mbConnectionAccepted; }
	const char* GetRejectionReason() const { return mpcRejectionReason[0] != '\0' ? mpcRejectionReason : nullptr; }
	bool WasDisconnected() const { return mbDisconnectedEvent; }
	std::vector<ReceivedAssignment>& DrainReceivedAssignments() { return mReceivedAssignments; }
	std::vector<ReceivedPlayerState>& DrainReceivedPlayerStates() { return mReceivedPlayerStates; }

	const std::vector<ClientCoordSlot>& GetCoordSlots() const { return mCoordSlots; }
	std::vector<ClientCoordSlot>& GetCoordSlots() { return mCoordSlots; }
	ENetPeer* GetServerPeer() const { return mpServerPeer; }
	int64_t GetBytesInPerSecond() { return mBytesInPerSecond.Get(); }
	int64_t GetBytesOutPerSecond() { return mBytesOutPerSecond.Get(); }
	int64_t GetPipelineRttUs() { return mSmoothedPipelineRttUs.Get(); }

private:

	void HandleReceive(ENetEvent& rEvent);
	void HandleReceive(const uint8_t* pData, size_t iSize);
	void HandleServerAssignPlayer(const uint8_t* pData);
	void HandleServerCoordFullState(const uint8_t* pData);
	void HandleServerCoordUpdateOrResend(const uint8_t* pData, bool bProcessRtt);
	void HandleServerDebugFrame(const uint8_t* pData);
	void HandleServerConnectionResponse(const uint8_t* pData, size_t iSize);
	void HandleServerPlayerState(const uint8_t* pData);
	void HandleServerSubscribeAccept(const uint8_t* pData);
	void HandleServerUnsubscribeAck(const uint8_t* pData);
	void SendHello();

	void ClearSubscribingPlaceholder(GridCoord coord);
	void TrackReceivedTick(int64_t iSlot, int64_t iTick);

	ENetHost* mpHost = nullptr;
	ENetPeer* mpServerPeer = nullptr;
	bool mbConnected = false;
	bool mbConnectionAccepted = false;
	bool mbDisconnectedEvent = false;
	char mpcRejectionReason[256] = {};

	std::vector<ReceivedAssignment> mReceivedAssignments;
	std::vector<ReceivedPlayerState> mReceivedPlayerStates;

	// Per-slot receive buffers
	std::vector<std::vector<ReceivedCoordUpdate>> mReceivedCoordUpdates;
	std::vector<ReceivedCoordFullState> mReceivedFullStates;

	std::unique_ptr<ReceivedDebugFrame> mpReceivedDebugFrame;

	// Per-slot subscription state (replaces single ACK floor/bitfield)
	std::vector<ClientCoordSlot> mCoordSlots;

	// Pipeline RTT (timestamp echo)
	common::Smoothed<int64_t> mSmoothedPipelineRttUs;
	int64_t miLastEchoedTimestampNs = 0;

	// Bandwidth tracking (host-level cumulative counters)
	uint32_t muiPrevReceivedData = 0;
	uint32_t muiPrevSentData = 0;
	common::InTheLastSecond mBytesInPerSecond;
	common::InTheLastSecond mBytesOutPerSecond;

	bool mbDesyncDebugMode = false;

	// Network simulation delay queue
	std::deque<DelayedPacket> mDelayedPackets;
};

inline ClientNetwork* gpClientNetwork = nullptr;

} // namespace engine
