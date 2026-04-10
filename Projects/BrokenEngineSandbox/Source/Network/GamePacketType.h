#pragma once

namespace game
{

enum class GamePacketType : uint8_t
{
	kServerAssignPlayer = static_cast<uint8_t>(engine::PacketType::kGamePacketStart),
	kServerPlayerState,
	kClientUpdatePlayerRequest,
	kClientCreateFleetRequest,
	kClientSpawnIntoFleetRequest,
	kClientRespawnInFleetRequest,
	kServerFleetSync,
	kClientDeleteFleetRequest,
	kClientFleetNavigationDelay,
};

} // namespace game
