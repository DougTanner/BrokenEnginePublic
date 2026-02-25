#include "Pch.h"

#include "Network/NetworkClient.h"

#include "Frame/Frame.h"
#include "Memory/MemoryManager.h"
#include "Network/NetworkSerialization.h"

namespace engine
{

// Cursor read helpers
static void ReadBytes(const uint8_t*& pCursor, void* pDest, int64_t iSize)
{
	std::memcpy(pDest, pCursor, iSize);
	pCursor += iSize;
}

static int16_t ReadInt16(const uint8_t*& pCursor)
{
	int16_t i = 0;
	ReadBytes(pCursor, &i, sizeof(int16_t));
	return i;
}

static int32_t ReadInt32(const uint8_t*& pCursor)
{
	int32_t i = 0;
	ReadBytes(pCursor, &i, sizeof(int32_t));
	return i;
}

static uint32_t ReadUint32(const uint8_t*& pCursor)
{
	uint32_t ui = 0;
	ReadBytes(pCursor, &ui, sizeof(uint32_t));
	return ui;
}

static int64_t ReadInt64(const uint8_t*& pCursor)
{
	int64_t i = 0;
	ReadBytes(pCursor, &i, sizeof(int64_t));
	return i;
}

static GridCoord ReadGridCoord(const uint8_t*& pCursor)
{
	GridCoord coord {};
	coord.x = ReadInt32(pCursor);
	coord.y = ReadInt32(pCursor);
	return coord;
}

NetworkClient::NetworkClient(const char* pServerAddress, uint16_t uiPort)
{
	gpNetworkClient = this;

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates host data internally
	mpHost = enet_host_create(nullptr, 1, NetworkManager::kuiChannelCount, 0, 0);

	ENetAddress address {};
	enet_address_set_host(&address, pServerAddress);
	address.port = uiPort;

	// Heap: ENet allocates peer data internally
	mpServerPeer = enet_host_connect(mpHost, &address, NetworkManager::kuiChannelCount, 0);
}

NetworkClient::~NetworkClient()
{
	if (mpServerPeer != nullptr && mbConnected)
	{
		enet_peer_disconnect(mpServerPeer, 0);

		// Allow time for disconnect to be sent
		ENetEvent event {};
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		while (enet_host_service(mpHost, &event, 100) > 0)
		{
			if (event.type == ENET_EVENT_TYPE_DISCONNECT)
			{
				break;
			}
			if (event.type == ENET_EVENT_TYPE_RECEIVE)
			{
				enet_packet_destroy(event.packet);
			}
		}
	}

	if (mpHost != nullptr)
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		enet_host_destroy(mpHost);
	}

	gpNetworkClient = nullptr;
}

void NetworkClient::Poll()
{
	if (mpHost == nullptr)
	{
		return;
	}

	mReceivedUpdates.clear();
	mReceivedFullStates.clear();

	ENetEvent event {};
	while (enet_host_service(mpHost, &event, 0) > 0)
	{
		switch (event.type)
		{
		case ENET_EVENT_TYPE_CONNECT:
			mbConnected = true;
			if constexpr (kbEnableLogging)
			{
				common::Log("NetworkClient: Connected to server");
			}
			break;
		case ENET_EVENT_TYPE_DISCONNECT:
			mbConnected = false;
			mpServerPeer = nullptr;
			if constexpr (kbEnableLogging)
			{
				common::Log("NetworkClient: Disconnected from server");
			}
			break;
		case ENET_EVENT_TYPE_RECEIVE:
			HandleReceive(event);
			enet_packet_destroy(event.packet);
			break;
		case ENET_EVENT_TYPE_NONE:
			break;
		}
	}
}

void NetworkClient::HandleReceive(ENetEvent& rEvent)
{
	const uint8_t* pData = rEvent.packet->data;
	size_t iSize = rEvent.packet->dataLength;

	if (iSize < 1)
	{
		return;
	}

	PacketType eType = static_cast<PacketType>(pData[0]);

	switch (eType)
	{
	case PacketType::kServerAssignPlayer:
		HandleServerAssignPlayer(pData, iSize);
		break;
	case PacketType::kServerFullState:
		HandleServerFullState(pData, iSize);
		break;
	case PacketType::kServerUpdateStream:
		HandleServerUpdateStream(pData, iSize);
		break;
	default:
		break;
	}
}

void NetworkClient::HandleServerAssignPlayer(const uint8_t* pData, [[maybe_unused]] size_t iSize)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iPlayerIdValue = ReadInt64(pCursor);
	mAssignedPlayerId = game::player_t(uuid_t(iPlayerIdValue));

	mAssignedGridCoord = ReadGridCoord(pCursor);

	if constexpr (kbEnableLogging)
	{
		common::Log("NetworkClient: Assigned player ID {} at grid ({},{})", iPlayerIdValue, mAssignedGridCoord.x, mAssignedGridCoord.y);
	}
}

void NetworkClient::HandleServerFullState(const uint8_t* pData, [[maybe_unused]] size_t iSize)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iFrame = ReadInt64(pCursor);
	int16_t iCoordCount = ReadInt16(pCursor);

	for (int16_t c = 0; c < iCoordCount; ++c)
	{
		GridCoord coord = ReadGridCoord(pCursor);
		int32_t iUncompressedSize = ReadInt32(pCursor);
		int32_t iCompressedSize = ReadInt32(pCursor);

		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

		// Heap: temporary buffer for LZ4 decompression
		std::vector<char> decompressed(iUncompressedSize);
		LZ4_decompress_safe(
			reinterpret_cast<const char*>(pCursor),
			decompressed.data(),
			iCompressedSize,
			iUncompressedSize);
		pCursor += iCompressedSize;

		// Deserialize frame from decompressed data
		// Heap: stringstream allocates for frame deserialization
		std::string frameStr(decompressed.begin(), decompressed.end());
		std::istringstream frameStream(frameStr, std::ios::binary);

		// Heap: Frame allocation
		auto pFrame = std::make_unique<game::Frame>();
		frameStream >> *pFrame;

		ReceivedFullState fullState {};
		fullState.iFrame = iFrame;
		fullState.coord = coord;
		fullState.pFrame = std::move(pFrame);

		// Heap: received full states vector grows on new cell data
		mReceivedFullStates.push_back(std::move(fullState));
	}
}

void NetworkClient::HandleServerUpdateStream(const uint8_t* pData, [[maybe_unused]] size_t iSize)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iFrame = ReadInt64(pCursor);

	// Current frame
	int16_t iGridCount = ReadInt16(pCursor);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	ReceivedUpdate update {};
	update.iFrame = iFrame;
	// Heap: grid updates vector, variable per frame
	update.gridUpdates.resize(iGridCount);

	for (int16_t g = 0; g < iGridCount; ++g)
	{
		ReceivedGridUpdate& rGridUpdate = update.gridUpdates.at(g);
		rGridUpdate.coord = ReadGridCoord(pCursor);

		uint32_t uiServerCrc = ReadUint32(pCursor);
		rGridUpdate.serverCrc = static_cast<common::crc_t>(uiServerCrc);

		int32_t iCompSize = ReadInt32(pCursor);

		if (iCompSize > 0)
		{
			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			game::StatusChange* pStatusChanges = rWorkbuffer.PushBuffer<game::StatusChange*>(kiMaxStatusChangesPerCell * static_cast<int64_t>(sizeof(game::StatusChange)));
			int64_t iCount = DecompressStatusChangeBatch(pCursor, iCompSize, pStatusChanges, kiMaxStatusChangesPerCell);
			pCursor += iCompSize;

			// Heap: status changes vector per grid cell
			rGridUpdate.statusChanges.assign(pStatusChanges, pStatusChanges + iCount);
			rWorkbuffer.Pop();
		}
	}

	// Heap: received updates vector grows each tick
	mReceivedUpdates.push_back(std::move(update));
	TrackReceivedFrame(iFrame);

	// Re-sent frames
	int16_t iResendCount = ReadInt16(pCursor);
	for (int16_t r = 0; r < iResendCount; ++r)
	{
		int64_t iResendFrame = ReadInt64(pCursor);
		int16_t iResendGridCount = ReadInt16(pCursor);

		ReceivedUpdate resendUpdate {};
		resendUpdate.iFrame = iResendFrame;
		// Heap: grid updates vector for re-sent frame
		resendUpdate.gridUpdates.resize(iResendGridCount);

		for (int16_t g = 0; g < iResendGridCount; ++g)
		{
			ReceivedGridUpdate& rGridUpdate = resendUpdate.gridUpdates.at(g);
			rGridUpdate.coord = ReadGridCoord(pCursor);

			uint32_t uiResendCrc = ReadUint32(pCursor);
			rGridUpdate.serverCrc = static_cast<common::crc_t>(uiResendCrc);

			int32_t iResendCompSize = ReadInt32(pCursor);

			if (iResendCompSize > 0)
			{
				common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
				game::StatusChange* pStatusChanges = rWorkbuffer.PushBuffer<game::StatusChange*>(kiMaxStatusChangesPerCell * static_cast<int64_t>(sizeof(game::StatusChange)));
				int64_t iCount = DecompressStatusChangeBatch(pCursor, iResendCompSize, pStatusChanges, kiMaxStatusChangesPerCell);
				pCursor += iResendCompSize;

				rGridUpdate.statusChanges.assign(pStatusChanges, pStatusChanges + iCount);
				rWorkbuffer.Pop();
			}
		}

		// Heap: received updates vector grows for re-sent frames
		mReceivedUpdates.push_back(std::move(resendUpdate));
		TrackReceivedFrame(iResendFrame);
	}
}

void NetworkClient::TrackReceivedFrame(int64_t iFrame)
{
	if (iFrame > miHighestReceivedFrame)
	{
		// Add any missing frames between the last highest and this one
		for (int64_t i = miHighestReceivedFrame + 1; i < iFrame; ++i)
		{
			ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
			// Heap: missing frames vector grows on gap detection
			mMissingFrames.push_back(i);
		}
		miHighestReceivedFrame = iFrame;
	}

	// Remove this frame from missing list if present
	for (size_t i = 0; i < mMissingFrames.size(); ++i)
	{
		if (mMissingFrames.at(i) == iFrame)
		{
			mMissingFrames.at(i) = mMissingFrames.back();
			mMissingFrames.pop_back();
			break;
		}
	}
}

void NetworkClient::SendInput(uint16_t uiPlayerId, const game::PlayerInput& rInput, bool bGamepad, float fRotateEye)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][2B playerId]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientInputStream));
	rWorkbuffer.PushBack<uint16_t>(uiPlayerId);

	// Held flags
	uint64_t uiHeldFlags = 0;
	std::memcpy(&uiHeldFlags, &rInput.flags, sizeof(uint64_t));
	rWorkbuffer.PushBack<uint64_t>(uiHeldFlags);

	// Movement
	rWorkbuffer.PushBack<XMFLOAT3>(rInput.f3Move);

	// Direction (as XMFLOAT4A)
	XMFLOAT4A f4Direction {};
	XMStoreFloat4A(&f4Direction, rInput.vecDirection);
	rWorkbuffer.PushBack<XMFLOAT4A>(f4Direction);

	// Gamepad + rotateEye
	uint8_t uiGamepad = bGamepad ? 1 : 0;
	rWorkbuffer.PushBack<uint8_t>(uiGamepad);
	rWorkbuffer.PushBack<float>(fRotateEye);

	// Re-send request list
	int16_t iMissingCount = static_cast<int16_t>(std::min(static_cast<size_t>(kiMaxResendFrames), mMissingFrames.size()));
	rWorkbuffer.PushBack<int16_t>(iMissingCount);
	for (int16_t i = 0; i < iMissingCount; ++i)
	{
		rWorkbuffer.PushBack<int64_t>(mMissingFrames.at(i));
	}

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), 0);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelUnreliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void NetworkClient::SendSpawnRequest(ClientRequestFlags_t flags)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientSpawnRequest));
	uint8_t uiFlags = 0;
	std::memcpy(&uiFlags, &flags, sizeof(uint8_t));
	rWorkbuffer.PushBack<uint8_t>(uiFlags);

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void NetworkClient::SendDesyncReport(int64_t iFrame, GridCoord coord, common::crc_t expected, common::crc_t actual)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientDesyncReport));
	rWorkbuffer.PushBack<int64_t>(iFrame);
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);
	rWorkbuffer.PushBack<uint32_t>(static_cast<uint32_t>(expected));
	rWorkbuffer.PushBack<uint32_t>(static_cast<uint32_t>(actual));

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

} // namespace engine
