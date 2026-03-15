#include "Game.h"

#include "Network/PlayerEvents.h"

#include "Network/NetworkCursor.h"
#include "Network/NetworkProtocol.h"

namespace game
{

void ParsePlayerEvents(
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& rRawPackets,
	std::vector<ReceivedPlayerEvent>& rOutEvents)
{
	for (const auto& [uiPacketType, rPayload] : rRawPackets)
	{
		engine::PacketType eType = static_cast<engine::PacketType>(uiPacketType);

		if (eType == engine::PacketType::kServerAssignPlayer)
		{
			// 8B playerId + 4B gridX + 4B gridY = 16 bytes (type byte already stripped)
			if (rPayload.size() < 16)
			{
				continue;
			}
			const uint8_t* pCursor = rPayload.data();
			int64_t iPlayerId = engine::ReadInt64(pCursor);
			engine::GridCoord coord = engine::ReadGridCoord(pCursor);
			rOutEvents.push_back({PlayerEventType::kAssigned, player_t(engine::uuid_t(iPlayerId)), coord});
		}
		else if (eType == engine::PacketType::kServerPlayerState)
		{
			// 1B wireType + 8B playerId + 4B gridX + 4B gridY = 17 bytes (type byte already stripped)
			if (rPayload.size() < 17)
			{
				continue;
			}
			const uint8_t* pCursor = rPayload.data();
			uint8_t uiWireType = engine::ReadUint8(pCursor);
			int64_t iPlayerId = engine::ReadInt64(pCursor);
			engine::GridCoord coord = engine::ReadGridCoord(pCursor);

			// Wire type maps directly to PlayerEventType offset by 1 (kAssigned=0 has no wire equivalent)
			PlayerEventType eEventType = static_cast<PlayerEventType>(uiWireType + 1);
			rOutEvents.push_back({eEventType, player_t(engine::uuid_t(iPlayerId)), coord});
		}
	}
}

} // namespace game
