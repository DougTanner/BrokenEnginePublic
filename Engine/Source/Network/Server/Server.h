#pragma once

#if defined(BT_SERVER)

#include "Frame/GridCoord.h"
#include "Network/NetworkCursor.h"
#include "Network/Server/ServerTypes.h"

namespace engine
{

struct FrameStaticData;

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
	ENetPeer* pPeer = nullptr;
	int64_t iClientId = 0;
	bool bHandshakeComplete = false;
	std::vector<GridCoord> authorizedCoords;
	ClientGuid clientGuid {};

	// Slot-based coord subscriptions with independent ACK tracking
	std::vector<ClientCoordSubscription> coordSubscriptions;
	std::vector<AckState> coordAckStates;

	// Pipeline RTT: echoed back to client in update packets
	int64_t iClientTimestampNs = 0;

	// Delta-only resend logging: previous resend count per slot
	std::vector<int64_t> prevResendCounts;
	std::vector<int64_t> resendLogCooldowns;

	// Delta-only floor advance logging: consecutive zero-advance ACK count
	int64_t iConsecutiveZeroAdvanceAcks = 0;
	bool bFloorStalled = false;
	int64_t iPeakConsecutiveStallAcks = 0;

	// Client->server contract enforcement (see NetworkProtocol.h / Server::RecordContractViolation)
	int64_t iContractViolations = 0;            // lifetime, never reset
	int64_t iTickPacketCount = 0;               // reset per poll window (Server::Poll)
	int64_t iTickByteCount = 0;                 // reset per poll window (Server::Poll)
	uint16_t tickTypeCounts[256] {}; // per-type count this poll window, indexed by raw type byte; reset in Poll

	// Helpers
	int64_t FindSlotForCoord(GridCoord coord) const
	{
		for (int64_t i = 0; i < std::ssize(coordSubscriptions); ++i)
		{
			if ((coordSubscriptions.at(i).flags & SubscriptionFlags::kActive) && coordSubscriptions.at(i).coord == coord)
			{
				return i;
			}
		}
		return -1;
	}

	int64_t AllocateSlot(int64_t iMaxSlots)
	{
		int64_t iLimit = std::min(iMaxSlots, std::ssize(coordSubscriptions));
		for (int64_t i = 0; i < iLimit; ++i)
		{
			if (!(coordSubscriptions.at(i).flags & SubscriptionFlags::kActive))
			{
				return i;
			}
		}
		return -1;
	}

	void FreeSlot(int64_t iSlot)
	{
		coordSubscriptions.at(iSlot) = {};
		// Reset ACK state but preserve epoch (incremented on next allocation)
		uint16_t uiEpoch = coordAckStates.at(iSlot).uiEpoch;
		coordAckStates.at(iSlot) = {};
		coordAckStates.at(iSlot).uiEpoch = uiEpoch;
		if (iSlot < std::ssize(prevResendCounts))
		{
			prevResendCounts.at(iSlot) = 0;
		}
		if (iSlot < std::ssize(resendLogCooldowns))
		{
			resendLogCooldowns.at(iSlot) = 0;
		}
	}

	bool IsCoordSubscribed(GridCoord coord) const
	{
		return FindSlotForCoord(coord) >= 0;
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

class Server
{
public:

	Server(uint16_t uiPort);
	~Server();

	void Poll();

	template <typename TType, typename... TArgs>
	void SendSimplePacket(ENetPeer* pPeer, TType eType, uint8_t uiChannel, uint32_t uiPacketFlags, const TArgs&... args)
	{
		static_assert(std::is_enum_v<TType>, "SendSimplePacket type tag must be an enum (engine::PacketType or game::GamePacketType)");

		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

		rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(eType));
		(PushSimplePacketArg(rWorkbuffer, args), ...);

		NetworkManager::SendPacket(pPeer, uiChannel, rWorkbuffer, uiPacketFlags);
	}

	void SendCoordFullState(int64_t iClientId, int64_t iSlot, int64_t iTick, GridCoord coord, const game::Frame* pFrame);
	void SendCoordStaticData(int64_t iClientId, int64_t iSlot, GridCoord coord, const FrameStaticData& rStaticData);
	void BufferFrame(int64_t iTick, std::span<const std::pair<GridCoord, GridUpdateData>> gridUpdates);
	void BufferFullFrame(int64_t iTick, std::span<const std::pair<GridCoord, const game::Frame*>> frames);
	void SendUpdate(ClientConnection& rClient, int64_t iTick);
	void SendResends(ClientConnection& rClient, int64_t iTick);
	void Flush();
	std::vector<PendingSpawnRequest>& DrainPendingSpawnRequests() { return mPendingSpawnRequests; }
	std::vector<PendingDisconnect>& DrainPendingDisconnects() { return mPendingDisconnects; }
	std::vector<PendingNewSubscription>& DrainPendingNewSubscriptions() { return mPendingNewSubscriptions; }
	std::vector<int64_t>& DrainPendingResyncClientIds() { return mPendingResyncClientIds; }
	std::vector<ReceivedGamePacket>& DrainReceivedGamePackets() { return mReceivedGamePackets; }
	const std::vector<ClientConnection>& GetClients() const { return mClients; }
	std::vector<ClientConnection>& GetClients() { return mClients; }
	ClientConnection* FindClient(int64_t iClientId);
	const ClientConnection* FindClient(int64_t iClientId) const;
	void BroadcastLoadNotification();
	void ClearBufferedFrames();

	// Records a client->server contract violation; escalates to disconnect at kiContractViolationDisconnectCount.
	// Callers MUST NOT touch their ClientConnection* afterward -- the client may have been removed.
	void RecordContractViolation(int64_t iClientId, const char* pcReason, uint8_t uiPacketType, int64_t iSize);

private:

	void Connect(ENetEvent& rEvent);
	void Disconnect(ENetEvent& rEvent);
	void DispatchIncoming(ENetEvent& rEvent);
	void Receive(ENetEvent& rEvent);
	void Receive(const uint8_t* pData, size_t iSize, ENetPeer* pPeer);

	ClientConnection* FindHandshakenClient(int64_t iClientId);

	void ClientAckStream(const uint8_t* pData, size_t iSize, int64_t iClientId);
	void ClientSpawnRequest(const uint8_t* pData, size_t iSize, int64_t iClientId);
	void ClientDesyncReport(const uint8_t* pData, size_t iSize, int64_t iClientId);
	void ClientDebugFrameRequest(const uint8_t* pData, size_t iSize, ENetPeer* pPeer, int64_t iClientId);
	void ClientHello(const uint8_t* pData, size_t iSize, ENetPeer* pPeer, int64_t iClientId);
	void ClientSubscribe(const uint8_t* pData, size_t iSize, int64_t iClientId);
	void ClientUnsubscribe(const uint8_t* pData, size_t iSize, int64_t iClientId);
	void ClientResyncRequest(int64_t iClientId);
	void SendConnectionResponse(ENetPeer* pPeer, bool bAccepted, const char* pMessage, const ClientGuid* pGuid);
	void SendSubscribeAccept(ClientConnection& rClient, int64_t iSlot, GridCoord coord);

	void WriteBufferedFramePacket(common::Workbuffer& rWorkbuffer, PacketType eType, int64_t iSlot, uint16_t uiEpoch, const PerCoordBufferedFrame& rBuffered, int64_t iTimestampNs);
	const PerCoordBufferedFrame* FindBufferedFrame(GridCoord coord, int64_t iTick) const;
	int CompressToBuffer(const char* pData, int iSize);
	void RemoveClient(int64_t iClientId);

	// SendResends helpers
	void UpdateResendLogState(ClientConnection& rClient, int64_t iSlot, int64_t iSlotResendCount, GridCoord coord);

	ENetHost* mpHost = nullptr;
	std::vector<ClientConnection> mClients;
	std::vector<PendingSpawnRequest> mPendingSpawnRequests;
	std::vector<PendingDisconnect> mPendingDisconnects;
	std::vector<PendingNewSubscription> mPendingNewSubscriptions;
	std::vector<int64_t> mPendingResyncClientIds;
	// Heap: raw game packets forwarded for game-layer parsing
	std::vector<ReceivedGamePacket> mReceivedGamePackets;
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
};

inline Server* gpServer = nullptr;

} // namespace engine

#endif // BT_SERVER
