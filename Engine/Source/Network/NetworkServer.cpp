#include "Pch.h"

#include "Network/NetworkServer.h"

#include "Input/Input.h"
#include "Memory/MemoryManager.h"

namespace engine
{

// Cursor read helpers
static void ReadBytes(const uint8_t*& pCursor, void* pDest, int64_t iSize)
{
	std::memcpy(pDest, pCursor, iSize);
	pCursor += iSize;
}

static uint8_t ReadUint8(const uint8_t*& pCursor)
{
	return *pCursor++;
}

static uint16_t ReadUint16(const uint8_t*& pCursor)
{
	uint16_t ui = 0;
	ReadBytes(pCursor, &ui, sizeof(uint16_t));
	return ui;
}

static int16_t ReadInt16(const uint8_t*& pCursor)
{
	int16_t i = 0;
	ReadBytes(pCursor, &i, sizeof(int16_t));
	return i;
}

static int64_t ReadInt64(const uint8_t*& pCursor)
{
	int64_t i = 0;
	ReadBytes(pCursor, &i, sizeof(int64_t));
	return i;
}

static float ReadFloat(const uint8_t*& pCursor)
{
	float f = 0.0f;
	ReadBytes(pCursor, &f, sizeof(float));
	return f;
}

static XMVECTOR ReadVec4(const uint8_t*& pCursor)
{
	XMFLOAT4A f4 {};
	ReadBytes(pCursor, &f4, sizeof(XMFLOAT4A));
	return XMLoadFloat4A(&f4);
}

static bool ReadBool(const uint8_t*& pCursor)
{
	return ReadUint8(pCursor) != 0;
}

static void WritePlayerInputs(common::Workbuffer& rWorkbuffer, std::span<const game::PlayerInput> playerInputs)
{
	int32_t iCount = static_cast<int32_t>(playerInputs.size());
	rWorkbuffer.PushBack<int32_t>(iCount);
	for (int32_t i = 0; i < iCount; ++i)
	{
		const game::PlayerInput& rInput = playerInputs[i];
		uint64_t uiFlags = 0;
		std::memcpy(&uiFlags, &rInput.flags, sizeof(uint64_t));
		rWorkbuffer.PushBack<uint64_t>(uiFlags);
		rWorkbuffer.PushBack<float>(rInput.f3Move.x);
		rWorkbuffer.PushBack<float>(rInput.f3Move.y);
		rWorkbuffer.PushBack<float>(rInput.f3Move.z);
		XMFLOAT4A f4Direction {};
		XMStoreFloat4A(&f4Direction, rInput.vecDirection);
		rWorkbuffer.PushBack<XMFLOAT4A>(f4Direction);
	}
}

static void ComputeActiveCoords(GridCoord center, std::vector<GridCoord>& rOut)
{
	rOut.clear();
	rOut.push_back(center);
	for (const GridCoord& rOffset : kNeighborOffsets)
	{
		rOut.push_back({center.x + rOffset.x, center.y + rOffset.y});
	}
}

NetworkServer::NetworkServer(uint16_t uiPort)
{
	gpNetworkServer = this;

	ENetAddress address {};
	address.host = ENET_HOST_ANY;
	address.port = uiPort;

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates host data internally
	mpHost = enet_host_create(&address, 64, NetworkManager::kuiChannelCount, 0, 0);
}

NetworkServer::~NetworkServer()
{
	if (mpHost != nullptr)
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		enet_host_destroy(mpHost);
	}

	gpNetworkServer = nullptr;
}

void NetworkServer::Poll()
{
	if (mpHost == nullptr)
	{
		return;
	}

	mPendingInputs.clear();
	mPendingSpawnRequests.clear();

	ENetEvent event {};
	while (enet_host_service(mpHost, &event, 0) > 0)
	{
		switch (event.type)
		{
			case ENET_EVENT_TYPE_CONNECT:
				HandleConnect(event);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				HandleDisconnect(event);
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

void NetworkServer::HandleConnect(ENetEvent& rEvent)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	ClientConnection connection {};
	connection.pPeer = rEvent.peer;
	connection.iClientId = miNextClientId++;
	connection.uiPlayerId = static_cast<uint16_t>(connection.iClientId);

	rEvent.peer->data = reinterpret_cast<void*>(connection.iClientId);

	// Heap: client vector grows on connect
	mClients.push_back(std::move(connection));

	common::Log("NetworkServer: Client {} connected", mClients.back().iClientId);
}

void NetworkServer::HandleDisconnect(ENetEvent& rEvent)
{
	int64_t iClientId = reinterpret_cast<int64_t>(rEvent.peer->data);

	for (size_t i = 0; i < mClients.size(); ++i)
	{
		if (mClients.at(i).iClientId == iClientId)
		{
			mClients.at(i) = std::move(mClients.back());
			mClients.pop_back();
			break;
		}
	}

	common::Log("NetworkServer: Client {} disconnected", iClientId);
}

void NetworkServer::HandleReceive(ENetEvent& rEvent)
{
	const uint8_t* pData = rEvent.packet->data;
	size_t iSize = rEvent.packet->dataLength;

	if (iSize < 1)
	{
		return;
	}

	int64_t iClientId = reinterpret_cast<int64_t>(rEvent.peer->data);
	PacketType eType = static_cast<PacketType>(pData[0]);

	switch (eType)
	{
		case PacketType::kClientInputStream:
			HandleClientInputStream(pData, iSize, iClientId);
			break;
		case PacketType::kClientSpawnRequest:
			HandleClientSpawnRequest(pData, iSize, iClientId);
			break;
		case PacketType::kClientDesyncReport:
			HandleClientDesyncReport(pData, iSize);
			break;
		default:
			break;
	}
}

void NetworkServer::HandleClientInputStream(const uint8_t* pData, [[maybe_unused]] size_t iSize, int64_t iClientId)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	[[maybe_unused]] uint16_t uiPlayerId = ReadUint16(pCursor);

	// Read held flags
	uint64_t uiHeldFlags = 0;
	ReadBytes(pCursor, &uiHeldFlags, sizeof(uint64_t));
	game::FrameInputHeldFlags_t heldFlags;
	std::memcpy(&heldFlags, &uiHeldFlags, sizeof(uint64_t));

	// Read movement
	XMFLOAT3 f3Move {};
	ReadBytes(pCursor, &f3Move, sizeof(XMFLOAT3));

	// Read direction
	XMVECTOR vecDirection = ReadVec4(pCursor);

	// Read gamepad and rotateEye
	bool bGamepad = ReadBool(pCursor);
	float fRotateEye = ReadFloat(pCursor);

	// Derive pressed flags from held state delta
	ClientConnection* pClient = FindClient(iClientId);
	game::FrameInputPressedFlags_t pressedFlags {};
	if (pClient != nullptr)
	{
		uint64_t uiPrevHeld = 0;
		std::memcpy(&uiPrevHeld, &pClient->previousHeldFlags, sizeof(uint64_t));
		uint64_t uiCurrHeld = 0;
		std::memcpy(&uiCurrHeld, &heldFlags, sizeof(uint64_t));

		// Rising edge: not held before, held now
		uint64_t uiRising = uiCurrHeld & ~uiPrevHeld;

		uint32_t uiPressed = 0;
		// kPrimary (0x0001) -> kTogglePrimary (0x0001)
		if (uiRising & 0x0001)
		{
			uiPressed |= 0x0001;
		}
		// kSecondary (0x0002) -> kToggleSecondary (0x0002)
		if (uiRising & 0x0002)
		{
			uiPressed |= 0x0002;
		}

		std::memcpy(&pressedFlags, &uiPressed, sizeof(uint32_t));
		pClient->previousHeldFlags = heldFlags;
	}

	// Read re-send requests
	int16_t iResendCount = ReadInt16(pCursor);
	if (pClient != nullptr)
	{
		pClient->pendingResendFrames.clear();
		for (int16_t i = 0; i < iResendCount; ++i)
		{
			pClient->pendingResendFrames.push_back(ReadInt64(pCursor));
		}
	}

	// Deduplicate: if we already have input from this client this tick, OR-merge pressedFlags and overwrite the rest
	PendingInput* pExisting = nullptr;
	for (PendingInput& rPending : mPendingInputs)
	{
		if (rPending.iClientId == iClientId)
		{
			pExisting = &rPending;
			break;
		}
	}

	if (pExisting != nullptr)
	{
		uint32_t uiExistingPressed = 0;
		std::memcpy(&uiExistingPressed, &pExisting->pressedFlags, sizeof(uint32_t));
		uint32_t uiNewPressed = 0;
		std::memcpy(&uiNewPressed, &pressedFlags, sizeof(uint32_t));
		uiExistingPressed |= uiNewPressed;
		std::memcpy(&pExisting->pressedFlags, &uiExistingPressed, sizeof(uint32_t));

		pExisting->heldFlags = heldFlags;
		pExisting->f3Move = f3Move;
		pExisting->vecDirection = vecDirection;
		pExisting->bGamepad = bGamepad;
		pExisting->fRotateEye = fRotateEye;
	}
	else
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: pending input vector grows each tick
		mPendingInputs.push_back({
			.iClientId = iClientId,
			.heldFlags = heldFlags,
			.f3Move = f3Move,
			.vecDirection = vecDirection,
			.bGamepad = bGamepad,
			.fRotateEye = fRotateEye,
			.pressedFlags = pressedFlags,
		});
	}
}

void NetworkServer::HandleClientSpawnRequest(const uint8_t* pData, [[maybe_unused]] size_t iSize, int64_t iClientId)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type
	uint8_t uiFlags = ReadUint8(pCursor);

	ClientRequestFlags_t flags;
	std::memcpy(&flags, &uiFlags, sizeof(uint8_t));

	common::Log("NetworkServer: Client {} spawn request (spawn={}, respawn={})",
		iClientId,
		static_cast<bool>(flags & ClientRequestFlags::kSpawnRequested),
		static_cast<bool>(flags & ClientRequestFlags::kRespawnRequested));

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: spawn request vector grows on request
	mPendingSpawnRequests.push_back({iClientId, flags});
}

void NetworkServer::HandleClientDesyncReport(const uint8_t* pData, [[maybe_unused]] size_t iSize)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iFrame = ReadInt64(pCursor);
	int32_t iGridX = 0;
	ReadBytes(pCursor, &iGridX, sizeof(int32_t));
	int32_t iGridY = 0;
	ReadBytes(pCursor, &iGridY, sizeof(int32_t));
	uint64_t uiExpectedCrc = 0;
	ReadBytes(pCursor, &uiExpectedCrc, sizeof(uint64_t));
	uint64_t uiActualCrc = 0;
	ReadBytes(pCursor, &uiActualCrc, sizeof(uint64_t));

	char pcExpected[20] {};
	char pcActual[20] {};
	common::Log("NetworkServer: Desync report frame {} grid ({},{}) expected={} actual={}",
		iFrame, iGridX, iGridY,
		common::ToHex(std::span(pcExpected), uiExpectedCrc),
		common::ToHex(std::span(pcActual), uiActualCrc));
}

void NetworkServer::SendAssignPlayer(int64_t iClientId, game::player_t playerId, GridCoord coord)
{
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	pClient->humanPlayerId = playerId;
	pClient->humanGridCoord = coord;

	std::vector<GridCoord> oldActiveCoords = std::move(pClient->activeCoords);
	ComputeActiveCoords(coord, pClient->activeCoords);

	// Coords in new set but not in old set need full state
	pClient->pendingFullStateCoords.clear();
	for (const GridCoord& rCoord : pClient->activeCoords)
	{
		if (!std::ranges::contains(oldActiveCoords, rCoord))
		{
			pClient->pendingFullStateCoords.push_back(rCoord);
		}
	}

	// Always include destination coord (client's speculative state may have diverged)
	if (!std::ranges::contains(pClient->pendingFullStateCoords, coord))
	{
		pClient->pendingFullStateCoords.push_back(coord);
	}

	common::Log("NetworkServer: Sending assign player to client {} (player={}, grid ({},{}))",
		iClientId, playerId.ToUuid().Value(), coord.x, coord.y);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][8B player_t ID][GridCoord]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerAssignPlayer));
	rWorkbuffer.PushBack<int64_t>(playerId.ToUuid().Value());
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates packet data internally
	ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(pClient->pPeer, NetworkManager::kuiChannelReliable, pPacket);

	rWorkbuffer.Pop();
}

void NetworkServer::SendFullState(int64_t iClientId, int64_t iFrame, const std::vector<std::pair<GridCoord, const game::Frame*>>& rFrames)
{
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	common::Log("NetworkServer: Sending full state to client {} (frame {}, {} coords)",
		iClientId, iFrame, rFrames.size());

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// Header: [1B type][8B frame counter][2B coord count]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerFullState));
	rWorkbuffer.PushBack<int64_t>(iFrame);
	rWorkbuffer.PushBack<int16_t>(static_cast<int16_t>(rFrames.size()));

	// For each grid coord, serialize the Frame with LZ4 compression
	for (const auto& [coord, pFrame] : rFrames)
	{
		rWorkbuffer.PushBack<int32_t>(coord.x);
		rWorkbuffer.PushBack<int32_t>(coord.y);

		// Serialize frame to a temporary stringstream
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: stringstream allocates for frame serialization
		std::ostringstream frameStream(std::ios::binary);
		frameStream << *pFrame;
		std::string frameData = frameStream.str();

		// LZ4 compress
		int iMaxCompressed = LZ4_compressBound(static_cast<int>(frameData.size()));
		// Heap: temporary buffer for LZ4 compression
		std::vector<uint8_t> compressedBuffer(iMaxCompressed);
		int iCompressedSize = LZ4_compress_default(
			frameData.data(),
			reinterpret_cast<char*>(compressedBuffer.data()),
			static_cast<int>(frameData.size()),
			iMaxCompressed);

		rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(frameData.size()));
		rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(iCompressedSize));
		rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(compressedBuffer.data()), iCompressedSize));
	}

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(pClient->pPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void NetworkServer::BufferFrame(int64_t iFrame, const std::vector<std::pair<GridCoord, GridUpdateData>>& rGridUpdates)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	// Heap: ring buffer grows until steady state
	BufferedFrame buffered {};
	buffered.iFrame = iFrame;

	// Heap: temporary buffer for compression, reused across grid cells
	std::vector<uint8_t> tempBuffer(kiMaxPacketSize);

	for (const auto& [coord, updateData] : rGridUpdates)
	{
		BufferedGridData gridData {};
		gridData.coord = coord;
		gridData.serverCrc = updateData.serverCrc;

		if (!updateData.statusChanges.empty())
		{
			int64_t iCompressedSize = CompressStatusChangeBatch(updateData.statusChanges.data(), static_cast<int64_t>(updateData.statusChanges.size()), tempBuffer.data(), kiMaxPacketSize);
			gridData.compressedData.assign(tempBuffer.begin(), tempBuffer.begin() + iCompressedSize);
		}

		gridData.playerInputs.assign(updateData.playerInputs.begin(), updateData.playerInputs.end());

		buffered.gridData.push_back(std::move(gridData));
	}

	mBufferedFrames.push_back(std::move(buffered));
	while (static_cast<int64_t>(mBufferedFrames.size()) > kiMaxBufferedFrames)
	{
		mBufferedFrames.pop_front();
	}
}

void NetworkServer::SendUpdate(ClientConnection& rClient, int64_t iFrame, const std::vector<std::pair<GridCoord, GridUpdateData>>& rGridUpdates)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	// Heap: temporary buffer for compression
	std::vector<uint8_t> tempBuffer(kiMaxPacketSize);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// Header: [1B type][8B frame counter]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerUpdateStream));
	rWorkbuffer.PushBack<int64_t>(iFrame);

	// Current frame: [2B coord count][per coord: GridCoord, CRC, compressed size, data, playerInputs]
	int16_t iActiveCount = static_cast<int16_t>(rGridUpdates.size());
	rWorkbuffer.PushBack<int16_t>(iActiveCount);

	for (const auto& [coord, updateData] : rGridUpdates)
	{
		rWorkbuffer.PushBack<int32_t>(coord.x);
		rWorkbuffer.PushBack<int32_t>(coord.y);
		rWorkbuffer.PushBack<uint64_t>(updateData.serverCrc);

		if (!updateData.statusChanges.empty())
		{
			int64_t iCompressedSize = CompressStatusChangeBatch(updateData.statusChanges.data(), static_cast<int64_t>(updateData.statusChanges.size()), tempBuffer.data(), kiMaxPacketSize);
			rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(iCompressedSize));
			rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(tempBuffer.data()), iCompressedSize));
		}
		else
		{
			rWorkbuffer.PushBack<int32_t>(0);
		}

		// Player inputs
		WritePlayerInputs(rWorkbuffer, updateData.playerInputs);
	}

	// Piggyback re-sends (capped)
	int16_t iResendCount = 0;
	int64_t iResendCountToSend = std::min(static_cast<int64_t>(rClient.pendingResendFrames.size()), kiMaxResendFrames);

	// Count how many we can actually find in the buffer
	for (int64_t i = 0; i < iResendCountToSend; ++i)
	{
		int64_t iRequestedFrame = rClient.pendingResendFrames.at(i);
		for (const BufferedFrame& rBuf : mBufferedFrames)
		{
			if (rBuf.iFrame == iRequestedFrame)
			{
				++iResendCount;
				break;
			}
		}
	}

	rWorkbuffer.PushBack<int16_t>(iResendCount);

	for (int64_t i = 0; i < iResendCountToSend; ++i)
	{
		int64_t iRequestedFrame = rClient.pendingResendFrames.at(i);
		for (const BufferedFrame& rBuf : mBufferedFrames)
		{
			if (rBuf.iFrame != iRequestedFrame)
			{
				continue;
			}

			rWorkbuffer.PushBack<int64_t>(rBuf.iFrame);

			// Filter to client's active coords
			int16_t iGridCount = 0;
			for (const BufferedGridData& rGridData : rBuf.gridData)
			{
				for (const GridCoord& rActive : rClient.activeCoords)
				{
					if (rActive == rGridData.coord)
					{
						++iGridCount;
						break;
					}
				}
			}

			rWorkbuffer.PushBack<int16_t>(iGridCount);

			for (const BufferedGridData& rGridData : rBuf.gridData)
			{
				bool bGridActive = false;
				for (const GridCoord& rActive : rClient.activeCoords)
				{
					if (rActive == rGridData.coord)
					{
						bGridActive = true;
						break;
					}
				}
				if (!bGridActive)
				{
					continue;
				}

				rWorkbuffer.PushBack<int32_t>(rGridData.coord.x);
				rWorkbuffer.PushBack<int32_t>(rGridData.coord.y);
				rWorkbuffer.PushBack<uint64_t>(rGridData.serverCrc);

				int32_t iCompSize = static_cast<int32_t>(rGridData.compressedData.size());
				rWorkbuffer.PushBack<int32_t>(iCompSize);
				if (iCompSize > 0)
				{
					rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(rGridData.compressedData.data()), iCompSize));
				}

				// Player inputs
				WritePlayerInputs(rWorkbuffer, rGridData.playerInputs);
			}

			break;
		}
	}

	rClient.pendingResendFrames.clear();

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), 0);
		enet_peer_send(rClient.pPeer, NetworkManager::kuiChannelUnreliable, pPacket);
	}

	rWorkbuffer.Pop();
}

ClientConnection* NetworkServer::FindClient(int64_t iClientId)
{
	for (ClientConnection& rClient : mClients)
	{
		if (rClient.iClientId == iClientId)
		{
			return &rClient;
		}
	}
	return nullptr;
}

} // namespace engine
