#pragma once

#if defined(BT_CLIENT)

#include "Frame/FrameStaticData.h"
#include "Network/NetworkCursor.h"

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
	common::crc_t sharedCrc = 0;
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

// Per-coord received static data (sent once per subscription)
struct ReceivedStaticData
{
	int64_t iSlot = -1;
	GridCoord coord {};
	FrameStaticData staticData;
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
	AckState ackState;
};

struct ReceivedDebugFrame
{
	int64_t iTick = 0;
	GridCoord coord {};
	std::unique_ptr<game::Frame> pFrame;
};

class Client
{
public:

	Client(const char* pServerAddress, uint16_t uiPort, int64_t iCoordSlots);
	~Client();

	void Poll();

	template <typename TType, typename... TArgs>
	void SendSimplePacket(TType eType, uint8_t uiChannel, uint32_t uiPacketFlags, const TArgs&... args)
	{
		static_assert(std::is_enum_v<TType>, "SendSimplePacket type tag must be an enum (engine::PacketType or game::GamePacketType)");

		if (!CanSend())
		{
			return;
		}

		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

		rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(eType));
		(PushSimplePacketArg(rWorkbuffer, args), ...);

		NetworkManager::SendPacket(mpServerPeer, uiChannel, rWorkbuffer, uiPacketFlags);
	}

	void SendAck();
	void SendSpawnRequest(ClientRequestFlags_t flags);
	void SendDesyncReport(int64_t iTick, GridCoord coord, common::crc_t expected, common::crc_t actual);
	void SendDebugFrameRequest(int64_t iTick, GridCoord coord);
	bool SendSubscribe(GridCoord coord);
	void SendUnsubscribe(int64_t iSlot);
	void SendResyncRequest();
	void Flush();
	void Disconnect();
	void SetDesyncDebugMode(bool bEnabled) { mbDesyncDebugMode = bEnabled; }

	std::vector<std::vector<ReceivedCoordUpdate>>& DrainReceivedCoordUpdates() { return mReceivedCoordUpdates; }
	std::vector<ReceivedCoordFullState>& DrainReceivedFullStates() { return mReceivedFullStates; }
	std::vector<ReceivedStaticData>& DrainReceivedStaticData() { return mReceivedStaticData; }
	std::unique_ptr<ReceivedDebugFrame> DrainReceivedDebugFrame() { return std::move(mpReceivedDebugFrame); }

	bool IsConnected() const { return mbConnected; }
	bool CanSend() const { return mbConnected && mpServerPeer != nullptr; }
	bool IsConnectionAccepted() const { return mbConnectionAccepted; }
	const char* GetRejectionReason() const { return mpcRejectionReason[0] != '\0' ? mpcRejectionReason : nullptr; }
	bool WasDisconnected() const { return mbDisconnectedEvent; }
	// Heap: raw game packet buffer grows on assign/player-state packets
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& DrainReceivedGamePackets() { return mReceivedGamePackets; }

	const ClientGuid& GetClientGuid() const { return mClientGuid; }
	bool DrainLoadNotification() { bool b = mbLoadNotificationReceived; mbLoadNotificationReceived = false; return b; }
	const std::vector<ClientCoordSlot>& GetCoordSlots() const { return mCoordSlots; }
	std::vector<ClientCoordSlot>& GetCoordSlots() { return mCoordSlots; }
	std::vector<GridCoord>& GetCancelledSubscriptions() { return mCancelledSubscriptions; }
	ENetPeer* GetServerPeer() const { return mpServerPeer; }
	int64_t GetBytesInPerSecond() { return mBytesInPerSecond.Get(); }
	int64_t GetBytesOutPerSecond() { return mBytesOutPerSecond.Get(); }
	int64_t GetPipelineRttUs() { return mSmoothedPipelineRttUs.Get(); }
	float GetPacketLossPercent();
	int64_t GetJitterUs() { return mSmoothedJitterUs.Get(); }

private:

	void Receive(ENetEvent& rEvent);
	void Receive(const uint8_t* pData, size_t iSize);
	void ServerCoordFullState(const uint8_t* pData, size_t iSize);
	void ServerCoordStaticData(const uint8_t* pData, size_t iSize);
	void ServerCoordUpdateOrResend(const uint8_t* pData, size_t iSize, bool bProcessRtt);
	void ServerDebugFrame(const uint8_t* pData, size_t iSize);
	void ServerConnectionResponse(const uint8_t* pData, size_t iSize);
	void ServerSubscribeAccept(const uint8_t* pData, size_t iSize);
	void ServerUnsubscribeAck(const uint8_t* pData, size_t iSize);
	void ServerTimespeedUpdate(const uint8_t* pData, size_t iSize);
	void SendHello();

	enum class FullStateFlags : uint8_t
	{
		kClearPlaceholder = 1 << 0, // Full state arrived before SubscribeAccept; clear kSubscribing placeholder + adopt coord
		kRejectAsGhost    = 1 << 1, // Coord mismatch on kWaitingFullState/kSubscribing slot; send unsubscribe + log
		kCommit           = 1 << 2, // Caller proceeds to push fullState + activate slot
	};
	using FullStateFlags_t = common::Flags<FullStateFlags>;
	FullStateFlags_t ClassifyFullState(uint8_t uiSlotIndex, uint16_t uiEpoch, GridCoord coord);

	enum class CoordUpdateFlags : uint8_t
	{
		kCommit    = 1 << 0, // Caller proceeds to decompress + push update
		kTrackTick = 1 << 1, // Call TrackReceivedTick (set only on kActive; skipped on kWaitingFullState)
	};
	using CoordUpdateFlags_t = common::Flags<CoordUpdateFlags>;
	CoordUpdateFlags_t ClassifyCoordUpdate(uint8_t uiSlotIndex, uint16_t uiEpoch);

	enum class SubscribeAcceptFlags : uint8_t
	{
		kClearPlaceholder = 1 << 0, // Clear stale kSubscribing placeholder at any other slot
		kHealEpoch        = 1 << 1, // Active slot, same coord: update epoch (late accept after re-subscribe)
		kCommitInit       = 1 << 2, // Initialize slot to kWaitingFullState
		kRejectGhost      = 1 << 3, // State mismatch: send unsubscribe + RemoveCancelledSubscription + logs
	};
	using SubscribeAcceptFlags_t = common::Flags<SubscribeAcceptFlags>;
	SubscribeAcceptFlags_t ClassifySubscribeAccept(uint8_t uiSlotIndex, GridCoord coord);

	bool RemoveCancelledSubscription(GridCoord coord);
	void ClearSubscribingPlaceholder(GridCoord coord);
	void TrackReceivedTick(int64_t iSlot, int64_t iTick);

	ENetHost* mpHost = nullptr;
	ENetPeer* mpServerPeer = nullptr;
	bool mbConnected = false;
	bool mbConnectionAccepted = false;
	bool mbDisconnectedEvent = false;
	char mpcRejectionReason[256] = {};

	// Heap: raw game packets (type byte + payload) for game-layer parsing
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>> mReceivedGamePackets;

	// Per-slot receive buffers
	std::vector<std::vector<ReceivedCoordUpdate>> mReceivedCoordUpdates;
	std::vector<ReceivedCoordFullState> mReceivedFullStates;
	// Heap: static data received once per subscription
	std::vector<ReceivedStaticData> mReceivedStaticData;

	std::unique_ptr<ReceivedDebugFrame> mpReceivedDebugFrame;

	// Per-slot subscription state (replaces single ACK floor/bitfield)
	std::vector<ClientCoordSlot> mCoordSlots;

	// Pipeline RTT (timestamp echo)
	common::Smoothed<int64_t> mSmoothedPipelineRttUs;
	int64_t miLastEchoedTimestampNs = 0;
	int64_t miHelloSendTimeNs = 0;

	// Bandwidth tracking (host-level cumulative counters)
	uint32_t muiPrevReceivedData = 0;
	uint32_t muiPrevSentData = 0;
	common::InTheLastSecond mBytesInPerSecond;
	common::InTheLastSecond mBytesOutPerSecond;

	// Packet loss tracking
	common::InTheLastSecond mFramesReceived;

	// Interarrival jitter tracking
	std::chrono::high_resolution_clock::time_point mLastUpdateArrival {};
	bool mbHasLastUpdateArrival = false;
	common::Smoothed<int64_t> mSmoothedJitterUs;

	bool mbDesyncDebugMode = false;
	bool mbLoadNotificationReceived = false;
	ClientGuid mClientGuid {};

	// Network simulation delay queue
	std::deque<DelayedPacket> mDelayedPackets;

	// Coords whose kSubscribing slot was cancelled before the server responded
	std::vector<GridCoord> mCancelledSubscriptions;
};

inline Client* gpClient = nullptr;

} // namespace engine

#endif // BT_CLIENT
