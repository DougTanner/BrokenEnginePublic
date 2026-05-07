#pragma once

namespace engine
{

class NetworkManager
{
public:

	NetworkManager();
	~NetworkManager();

	// Channel 0: Control Reliable (handshake, subscribe/unsubscribe, spawn, assign, desync)
	// Channel 1: Control Unreliable (reserved)
	// Channel 2+: Coord slots (pairs of reliable/unreliable per slot)
	static constexpr uint8_t kuiChannelReliable = 0;
	static constexpr uint8_t kuiChannelUnreliable = 1;

	static constexpr int64_t kiMaxEnetCoordSlots = 64;
	static constexpr uint8_t kuiChannelCount = 2 + static_cast<uint8_t>(kiMaxEnetCoordSlots) * 2; // 130

	static constexpr uint8_t CoordSlotReliable(int64_t iSlot) { return static_cast<uint8_t>(2 + iSlot * 2); }
	static constexpr uint8_t CoordSlotUnreliable(int64_t iSlot) { return static_cast<uint8_t>(2 + iSlot * 2 + 1); }
	static constexpr int64_t ChannelToSlot(uint8_t uiChannel) { return (uiChannel - 2) / 2; }
	static constexpr bool IsCoordChannel(uint8_t uiChannel) { return uiChannel >= 2; }
	static constexpr bool IsUnreliableChannel(uint8_t uiChannel) { return uiChannel == kuiChannelUnreliable || (IsCoordChannel(uiChannel) && (uiChannel % 2) == 1); }

	static inline void SendPacket(ENetPeer* pPeer, uint8_t uiChannel, common::Workbuffer& rWorkbuffer, uint32_t uiFlags)
	{
		std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();
		ScopedSuppressAllocationTracking suppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), uiFlags);
		enet_peer_send(pPeer, uiChannel, pPacket);
	}
};

inline NetworkManager* gpNetworkManager = nullptr;

} // namespace engine
