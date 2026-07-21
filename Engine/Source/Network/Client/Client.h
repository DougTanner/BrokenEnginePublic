#pragma once

#if defined(BT_CLIENT)

#include "Frame/FrameStaticData.h"
#include "Frame/GridCoord.h"
#include "Network/NetworkCursor.h"

namespace game
{

struct Frame;
struct StatusChange;

} // namespace game

namespace engine
{

class ClientSessionRuntime;

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
	std::unique_ptr<game::Frame> pFrame;
};

// Per-coord received static data (sent once per subscription)
struct ReceivedStaticData
{
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
	std::chrono::steady_clock::time_point transitionStartTime {};
};

struct ReceivedDebugFrame
{
	std::unique_ptr<game::Frame> pFrame;
};

class Client
{
public:

	using GuidAssignedCallback = void (*)(const ClientGuid&);

	Client(const char* pServerAddress, uint16_t uiPort, int64_t iCoordSlots, const ClientGuid& rGuid, GuidAssignedCallback pfnGuidAssigned);
	~Client();

	template <typename TType, typename... TArgs>
	void SendSimplePacket(TType eType, uint8_t uiChannel, uint32_t uiPacketFlags, const TArgs&... args)
	{
		static_assert(std::is_enum_v<TType>, "SendSimplePacket type tag must be an enum (engine::PacketType or game::GamePacketType)");

		if (!(mStateFlags & ClientStateFlags::kConnected) || mpServerPeer == nullptr)
		{
			return;
		}

		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

		rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(eType));
		(PushSimplePacketArg(rWorkbuffer, args), ...);

		NetworkManager::SendPacket(mpServerPeer, uiChannel, rWorkbuffer, uiPacketFlags);
	}

	void SendSpawnRequest(ClientRequestFlags_t flags);
	void SendDesyncReport(int64_t iTick, GridCoord coord, common::crc_t expected, common::crc_t actual);
	void SendDebugFrameRequest(int64_t iTick, GridCoord coord);
	void SendResyncRequest();
	void Disconnect();

	enum class ClientStateFlags : uint8_t
	{
		kConnected                = 1 << 0,
		kConnectionAccepted       = 1 << 1,
		kDisconnectedEvent        = 1 << 2,
		kHasLastUpdateArrival     = 1 << 3,
		kDesyncDebugMode          = 1 << 4,
		kLoadNotificationReceived = 1 << 5,
	};

	ENetPeer* mpServerPeer = nullptr;
	common::Flags<ClientStateFlags> mStateFlags;
	char mpcRejectionReason[256] = {};

	// Heap: raw game packet buffer grows on assign/player-state packets
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>> mReceivedGamePackets;
	std::vector<std::vector<ReceivedCoordUpdate>> mReceivedCoordUpdates;
	std::vector<ReceivedCoordFullState> mReceivedFullStates;
	std::vector<ReceivedStaticData> mReceivedStaticData;
	std::unique_ptr<ReceivedDebugFrame> mpReceivedDebugFrame;
	std::vector<ClientCoordSlot> mCoordSlots;
	common::Smoothed<int64_t> mSmoothedPipelineRttUs;
	common::InTheLastSecond mBytesInPerSecond;
	common::InTheLastSecond mBytesOutPerSecond;
	common::InTheLastSecond mFramesReceived;
	common::Smoothed<int64_t> mSmoothedJitterUs;
	ClientGuid mClientGuid {};
	NetworkTimeState mTimeState {};

private:
	friend class ClientSessionRuntime;
	void Poll(const NetworkTimeState& rTimeState);
	bool SendAck();
	bool SendSubscribe(GridCoord coord);
	void SendUnsubscribe(int64_t iSlot);
	void Flush();
	bool DrainLoadNotification()
	{
		bool bReceived = mStateFlags & ClientStateFlags::kLoadNotificationReceived;
		mStateFlags.Clear(ClientStateFlags::kLoadNotificationReceived);
		return bReceived;
	}
	void CancelSubscription(int64_t iSlot);
	void RecoverTimedOutSubscriptions();
	void ResetAllSlots();
	void DispatchIncoming(ENetEvent& rEvent, bool bFastForward);
	void Receive(ENetEvent& rEvent);
	void Receive(const uint8_t* pData, size_t iSize);
	void ServerCoordFullState(const uint8_t* pData, size_t iSize);
	void ServerCoordStaticData(const uint8_t* pData, size_t iSize);
	void ServerCoordUpdateOrResend(const uint8_t* pData, size_t iSize, bool bProcessRtt);
	void ServerDebugFrame(const uint8_t* pData, size_t iSize);
	void ServerConnectionResponse(const uint8_t* pData, size_t iSize);
	void ServerSubscribeAccept(const uint8_t* pData, size_t iSize);
	void ServerUnsubscribeAck(const uint8_t* pData, size_t iSize);
	void SendHello();

	enum class FullStateFlags : uint8_t
	{
		kClearPlaceholder = 1 << 0, // Full state arrived before SubscribeAccept; clear kSubscribing placeholder + adopt coord
		kRejectAsGhost    = 1 << 1, // No legitimate placeholder or coord mismatch; send epoch-qualified unsubscribe + log
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
	int64_t FindSubscribingPlaceholder(GridCoord coord) const;
	void ClearSubscribingPlaceholder(GridCoord coord);
	void TrackReceivedTick(int64_t iSlot, int64_t iTick);

	ENetHost* mpHost = nullptr;

	// Reused status-change decode scratch (same reused-buffer + exact-size-assign pattern as Server::mCompressionBuffer):
	// decompress into this 1024-cap buffer, then assign() the exact count into each ReceivedCoordUpdate so buffered updates carry no slack
	std::vector<game::StatusChange> mStatusChangeScratch;

	// Pipeline RTT (timestamp echo)
	int64_t miLastEchoedTimestampNs = 0;
	int64_t miHelloSendTimeNs = 0;

	// Bandwidth tracking (host-level cumulative counters)
	uint32_t muiPrevReceivedData = 0;
	uint32_t muiPrevSentData = 0;

	// Interarrival jitter tracking
	std::chrono::steady_clock::time_point mLastUpdateArrival {};

	// Tick-rate-locked ack cadence: last wall-clock ack send time; SendAck throttles to one sim-tick
	// interval so packet rate is decoupled from render framerate (interval derived from kiTickRate).
	std::chrono::steady_clock::time_point mLastAckSendTime {};

	GuidAssignedCallback mpfnGuidAssigned = nullptr;

	// Network simulation delay queue
	std::deque<DelayedPacket> mDelayedPackets;
	NetworkSimulationState mNetworkSimState;
	// Coords whose kSubscribing slot was cancelled before the server responded
	std::vector<GridCoord> mCancelledSubscriptions;
};

inline Client* gpClient = nullptr;

} // namespace engine

#endif // BT_CLIENT
