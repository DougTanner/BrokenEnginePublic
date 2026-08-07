#pragma once

#if defined(BT_SERVER)

#include "Frame/GridCoord.h"
#include "Network/NetworkCursor.h"
#include "Network/Server/ServerTypes.h"

namespace engine
{

struct FrameStaticData;
class ServerSessionRuntime;

} // namespace engine

namespace game
{

struct Frame;
struct StatusChange;

} // namespace game

namespace engine
{

struct ReceivedGamePacket
{
	int64_t iClientId = 0;
	uint8_t uiPacketType = 0;
	// Heap: raw game packet payload (type byte stripped)
	std::vector<uint8_t> payload;
};

struct ClientConnection
{
	struct SlotState
	{
		ClientCoordSubscription subscription {};
		AckState ack {};
		int64_t iPrevResendCount = 0;
		int64_t iResendLogCooldown = 0;
	};

	ENetPeer* pPeer = nullptr;
	int64_t iClientId = 0;
	bool bHandshakeComplete = false;
	std::vector<GridCoord> authorizedCoords;
	ClientGuid clientGuid {};

	// Slot-based coord subscriptions with independent ACK and resend tracking
	std::vector<SlotState> slots;

	// Pipeline RTT: echoed back to client in update packets
	int64_t iClientTimestampNs = 0;

	// Delta-only floor advance logging: consecutive zero-advance ACK count
	int64_t iConsecutiveZeroAdvanceAcks = 0;
	bool bFloorStalled = false;
	int64_t iPeakConsecutiveStallAcks = 0;

	// Independent wall-clock limits for expensive desync diagnostics
	std::chrono::steady_clock::time_point desyncReportDeadline {};
	std::chrono::steady_clock::time_point debugFrameRequestDeadline {};

	// Client->server contract enforcement (see NetworkProtocol.h / Server::RecordContractViolation)
	int64_t iContractViolations = 0;            // lifetime, never reset
	int64_t iTickPacketCount = 0;               // reset per update window (Server::Poll, kUpdateStart only)
	int64_t iTickByteCount = 0;                 // reset per update window (Server::Poll, kUpdateStart only)
	uint16_t tickTypeCounts[256] {}; // per-type count this update window, indexed by raw type byte; reset in Poll (kUpdateStart only)

	// Helpers
	int64_t FindSlotForCoord(GridCoord coord) const
	{
		for (int64_t i = 0; i < std::ssize(slots); ++i)
		{
			if ((slots.at(i).subscription.flags & SubscriptionFlags::kActive) && slots.at(i).subscription.coord == coord)
			{
				return i;
			}
		}
		return -1;
	}

	int64_t AllocateSlot(int64_t iMaxSlots)
	{
		int64_t iLimit = std::min(iMaxSlots, std::ssize(slots));
		for (int64_t i = 0; i < iLimit; ++i)
		{
			if (!(slots.at(i).subscription.flags & SubscriptionFlags::kActive))
			{
				return i;
			}
		}
		return -1;
	}

	void FreeSlot(int64_t iSlot)
	{
		SlotState& rSlot = slots.at(iSlot);
		// Reset ACK state but preserve epoch (incremented on next allocation)
		uint16_t uiEpoch = rSlot.ack.uiEpoch;
		rSlot = {};
		rSlot.ack.uiEpoch = uiEpoch;
	}

};

// Per-coord ring buffer entry for re-send support
struct PerCoordBufferedFrame
{
	int64_t iTick = 0;
	common::crc_t sharedCrc = 0;
	// Heap: variable-size compressed status change data per frame
	std::vector<uint8_t> compressedData;
};

struct BufferedFullFrame
{
	int64_t iTick = 0;
	// Heap: serialized frame data per grid coordinate for debug frame requests
	std::unordered_map<GridCoord, std::string> serializedFrames;
};

// Reusable std::streambuf that appends written bytes to the std::string mpTarget points at, reusing
// that string's capacity across serializations. Server main thread only (single-writer contract);
// point mpTarget at the destination before each use. Lets frame serialization write directly into
// recycled ring storage or a transient send scratch instead of a fresh ostringstream + .str() copy.
class StringAppendStreamBuf : public std::streambuf
{
public:

	std::string* mpTarget = nullptr;

protected:

	int_type overflow(int_type iChar) override
	{
		if (iChar != traits_type::eof())
		{
			mpTarget->push_back(static_cast<char>(iChar));
		}
		return traits_type::not_eof(iChar);
	}

	std::streamsize xsputn(const char_type* pData, std::streamsize iCount) override
	{
		mpTarget->append(pData, static_cast<size_t>(iCount));
		return iCount;
	}
};

// Which of an update's two Server::Poll calls is running. The tick-boundary poll continues the admission
// budget window the update-start poll opened, so a hostile client gets one budget window per update.
enum class ServerPollMode : uint8_t
{
	kUpdateStart,
	kTickBoundary,
};

class Server
{
public:

	Server(uint16_t uiPort, ServerSessionRuntime& rSessionRuntime);
	~Server();

	template <typename TType, typename... TArgs>
	void SendSimplePacket(ENetPeer* pPeer, TType eType, uint8_t uiChannel, uint32_t uiPacketFlags, const TArgs&... args)
	{
		static_assert(std::is_enum_v<TType>, "SendSimplePacket type tag must be an enum (engine::PacketType or game::GamePacketType)");

		NetworkManager::SendSimplePacket(pPeer, eType, uiChannel, uiPacketFlags, args...);
	}

	ClientConnection* FindClient(int64_t iClientId);
	const ClientConnection* FindClient(int64_t iClientId) const;
	void SendCoordFullState(int64_t iClientId, int64_t iSlot, int64_t iTick, GridCoord coord, const game::Frame* pFrame);
	void SendCoordStaticData(int64_t iClientId, int64_t iSlot, GridCoord coord, const FrameStaticData& rStaticData);
	void BroadcastLoadNotification();

	std::vector<ClientConnection> mClients;
	std::vector<PendingDisconnect> mPendingDisconnects;
	std::vector<PendingNewSubscription> mPendingNewSubscriptions;
	std::vector<int64_t> mPendingResyncClientIds;
	std::vector<ReceivedGamePacket> mReceivedGamePackets;

	// Records a client->server contract violation; escalates to disconnect at kiContractViolationDisconnectCount.
	// Callers MUST NOT touch their ClientConnection* afterward -- the client may have been removed.
	void RecordContractViolation(int64_t iClientId, const char* pcReason, uint8_t uiPacketType, int64_t iSize);

private:
	friend class ServerSessionRuntime;
	void Poll(const NetworkTimeState& rTimeState, ServerPollMode ePollMode);
	void BufferFrame(int64_t iTick, const std::pair<GridCoord, GridUpdateData>* pGridUpdates, int64_t iGridUpdateCount);
	void BufferFullFrame(int64_t iTick, const std::pair<GridCoord, const game::Frame*>* pFrames, int64_t iFrameCount);
	void SendUpdate(ClientConnection& rClient, int64_t iTick);
	void SendResends(ClientConnection& rClient, int64_t iTick);
	void Flush();
	void ClearBufferedFrames();

	void Connect(ENetEvent& rEvent);
	void Disconnect(ENetEvent& rEvent);
	void DispatchIncoming(ENetEvent& rEvent, bool bFastForward);
	void Receive(ENetEvent& rEvent);
	void Receive(std::span<const uint8_t> packetData, ENetPeer* pPeer);

	ClientConnection* FindHandshakenClient(int64_t iClientId);

	void ClientAckStream(std::span<const uint8_t> packetData, int64_t iClientId);
	void ClientDesyncReport(std::span<const uint8_t> packetData, int64_t iClientId);
	void ClientDebugFrameRequest(std::span<const uint8_t> packetData, ENetPeer* pPeer, int64_t iClientId);
	void ClientHello(std::span<const uint8_t> packetData, ENetPeer* pPeer, int64_t iClientId);
	void ClientSubscribe(std::span<const uint8_t> packetData, int64_t iClientId);
	void ClientUnsubscribe(std::span<const uint8_t> packetData, int64_t iClientId);
	void ClientResyncRequest(std::span<const uint8_t> packetData, int64_t iClientId);
	void SendConnectionResponse(ENetPeer* pPeer, bool bAccepted, const char* pMessage, const ClientGuid* pGuid);
	void SendSubscribeAccept(ClientConnection& rClient, int64_t iSlot, GridCoord coord);

	void WriteBufferedFramePacket(common::Workbuffer& rWorkbuffer, PacketType eType, int64_t iSlot, uint16_t uiEpoch, const PerCoordBufferedFrame& rBuffered, int64_t iTimestampNs);
	const PerCoordBufferedFrame* FindBufferedFrame(GridCoord coord, int64_t iTick) const;
	int CompressToBuffer(const char* pData, int iSize);
	void RemoveClient(int64_t iClientId);

	// SendResends helpers
	void UpdateResendLogState(ClientConnection& rClient, int64_t iSlot, int64_t iSlotResendCount, GridCoord coord);

	ENetHost* mpHost = nullptr;
	int64_t miNextClientId = 1;

	// Per-coord ring buffers for re-sends
	std::unordered_map<GridCoord, std::deque<PerCoordBufferedFrame>> mPerCoordBufferedFrames;
	int64_t miLatestBufferedTick = -1;

	// Ring buffer for debug frame requests
	std::deque<BufferedFullFrame> mBufferedFullFrames;

	// Compression scratch buffer (reused across BufferFrame calls)
	std::vector<uint8_t> mCompressionBuffer;

	// Reusable frame-serialization scratch (server main thread only - single-writer contract).
	// mFrameStream writes through mFrameStreamBuf into whatever string SetTarget points at: recycled
	// pool entries for the full-frame ring, or mSendScratch for the transient SendCoord* sends.
	StringAppendStreamBuf mFrameStreamBuf;
	std::ostream mFrameStream { &mFrameStreamBuf };
	std::string mSendScratch;
	// Heap: recycled per-coord buffers for the full-frame ring, reused across BufferFullFrame calls
	std::vector<std::string> mFullFramePool;

	// Network simulation delay queue
	std::deque<DelayedPacket> mDelayedPackets;
	NetworkSimulationState mNetworkSimState;
	ServerSessionRuntime& mrSessionRuntime;
};

inline Server* gpServer = nullptr;

} // namespace engine

#endif // BT_SERVER
