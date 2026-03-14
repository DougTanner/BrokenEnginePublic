#pragma once

#include "Network/Server/ServerTypes.h"

namespace game
{

struct Frame;
struct StatusChange;

} // namespace game

namespace engine
{

struct ClientConnection
{
	ENetPeer* pPeer = nullptr;
	int64_t iClientId = 0;
	game::player_t humanPlayerId {};
	GridCoord humanGridCoord {};

	// Slot-based subscriptions (replaces activeCoords + pendingFullStateCoords)
	std::vector<ClientCoordSubscription> coordSubscriptions;
	std::vector<AckState> coordAckStates;

	// Pipeline RTT: echoed back to client in update packets
	int64_t iClientTimestampNs = 0;

	// Helpers
	int64_t FindSlotForCoord(GridCoord coord) const
	{
		for (int64_t i = 0; i < std::ssize(coordSubscriptions); ++i)
		{
			if (coordSubscriptions.at(i).bActive && coordSubscriptions.at(i).coord == coord)
			{
				return i;
			}
		}
		return -1;
	}

	int64_t AllocateSlot()
	{
		for (int64_t i = 0; i < std::ssize(coordSubscriptions); ++i)
		{
			if (!coordSubscriptions.at(i).bActive)
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
	common::crc_t serverCrc = 0;
	common::crc_t inputCrc = 0;
	// Heap: variable-size compressed status change data per frame
	std::vector<uint8_t> compressedData;
};

struct BufferedFullFrame
{
	int64_t iTick = 0;
	// Heap: serialized frame data per grid coordinate for debug frame requests
	std::unordered_map<GridCoord, std::string> serializedFrames;
};

class Server
{
public:

	Server(uint16_t uiPort);
	~Server();

	void Poll();

	void SendAssignPlayer(int64_t iClientId, int64_t iPlayerId, GridCoord coord);
	void SendPlayerState(int64_t iClientId, uint8_t uiStateType, int64_t iPlayerId, GridCoord coord);
	void SendCoordFullState(int64_t iClientId, int64_t iSlot, int64_t iTick, GridCoord coord, const game::Frame* pFrame);
	void BufferFrame(int64_t iTick, const std::vector<std::pair<GridCoord, GridUpdateData>>& rGridUpdates);
	void BufferFullFrame(int64_t iTick, const std::vector<std::pair<GridCoord, const game::Frame*>>& rFrames);
	void SendUpdate(ClientConnection& rClient, int64_t iTick);
	void SendResends(ClientConnection& rClient, int64_t iTick);
	void Flush();
	std::vector<PendingSpawnRequest>& DrainPendingSpawnRequests() { return mPendingSpawnRequests; }
	std::vector<PendingDisconnect>& DrainPendingDisconnects() { return mPendingDisconnects; }
	std::vector<PendingNewSubscription>& DrainPendingNewSubscriptions() { return mPendingNewSubscriptions; }
	const std::vector<ClientConnection>& GetClients() const { return mClients; }
	std::vector<ClientConnection>& GetClients() { return mClients; }
	ClientConnection* FindClient(int64_t iClientId);
	const ClientConnection* FindClient(int64_t iClientId) const;

private:

	void Connect(ENetEvent& rEvent);
	void Disconnect(ENetEvent& rEvent);
	void Receive(ENetEvent& rEvent);
	void Receive(const uint8_t* pData, size_t iSize, ENetPeer* pPeer);

	void ClientAckStream(const uint8_t* pData, int64_t iClientId);
	void ClientSpawnRequest(const uint8_t* pData, int64_t iClientId);
	void ClientDesyncReport(const uint8_t* pData);
	void ClientDebugFrameRequest(const uint8_t* pData, ENetPeer* pPeer);
	void ClientHello(const uint8_t* pData, size_t iSize, ENetPeer* pPeer, int64_t iClientId);
	void ClientSubscribe(const uint8_t* pData, int64_t iClientId);
	void ClientUnsubscribe(const uint8_t* pData, int64_t iClientId);
	void SendConnectionResponse(ENetPeer* pPeer, bool bAccepted, const char* pMessage);
	void SendSubscribeAccept(ClientConnection& rClient, int64_t iSlot, GridCoord coord);
	void SendUnsubscribeAck(ClientConnection& rClient, int64_t iSlot);

	void WriteBufferedFramePacket(common::Workbuffer& rWorkbuffer, PacketType eType, int64_t iSlot, uint16_t uiEpoch, const PerCoordBufferedFrame& rBuffered, int64_t iTimestampNs);
	const PerCoordBufferedFrame* FindBufferedFrame(GridCoord coord, int64_t iTick) const;
	int CompressToBuffer(const char* pData, int iSize);
	void RemoveClient(int64_t iClientId);

	ENetHost* mpHost = nullptr;
	std::vector<ClientConnection> mClients;
	std::vector<PendingSpawnRequest> mPendingSpawnRequests;
	std::vector<PendingDisconnect> mPendingDisconnects;
	std::vector<PendingNewSubscription> mPendingNewSubscriptions;
	int64_t miNextClientId = 1;

	// Per-coord ring buffers for re-sends
	std::unordered_map<GridCoord, std::deque<PerCoordBufferedFrame>> mPerCoordBufferedFrames;
	int64_t miLatestBufferedTick = -1;

	// Ring buffer for debug frame requests
	std::deque<BufferedFullFrame> mBufferedFullFrames;

	// Compression scratch buffer (reused across BufferFrame calls)
	std::vector<uint8_t> mCompressionBuffer;

	// Network simulation delay queue
	std::deque<DelayedPacket> mDelayedPackets;
};

inline Server* gpServer = nullptr;

} // namespace engine
