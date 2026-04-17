#include "Network/PlayerEvents.h"

#include "Network/NetworkCursor.h"
#include "Network/NetworkProtocol.h"

#include "Fleet.h"
#include "Game.h"
#include "Network/GamePacketType.h"

namespace game
{

void ParsePlayerEvents(std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& rRawPackets, std::vector<ReceivedPlayerEvent>& rOutEvents)
{
	for (const auto& [uiPacketType, rPayload] : rRawPackets)
	{
		GamePacketType eType = static_cast<GamePacketType>(uiPacketType);

		if (eType == GamePacketType::kServerAssignPlayer)
		{
			// 8B playerId + 4B gridX + 4B gridY = 16 bytes (type byte already stripped)
			if (rPayload.size() < 16)
			{
				continue;
			}
			const uint8_t* pCursor = rPayload.data();
			int64_t iGlobalPlayerId = engine::ReadInt64(pCursor);
			engine::GridCoord coord = engine::ReadGridCoord(pCursor);
			rOutEvents.push_back({PlayerEventType::kAssigned, engine::global_id_t {iGlobalPlayerId}, coord});
		}
		else if (eType == GamePacketType::kServerPlayerState)
		{
			// 1B wireType + 8B global player ID + 4B gridX + 4B gridY = 17 bytes (type byte already stripped)
			if (rPayload.size() < 17)
			{
				continue;
			}
			const uint8_t* pCursor = rPayload.data();
			uint8_t uiWire = engine::ReadUint8(pCursor);
			int64_t iGlobalPlayerId = engine::ReadInt64(pCursor);
			engine::GridCoord coord = engine::ReadGridCoord(pCursor);

			PlayerEventType eEventType {};
			switch (static_cast<PlayerStateWireType>(uiWire))
			{
				case PlayerStateWireType::kSpawned:
					eEventType = PlayerEventType::kSpawned;
					break;
				case PlayerStateWireType::kChangedFrame:
					eEventType = PlayerEventType::kChangedFrame;
					break;
				case PlayerStateWireType::kDied:
					eEventType = PlayerEventType::kDied;
					break;
				default:
					DEBUG_BREAK();
					continue;
			}
			rOutEvents.push_back({eEventType, engine::global_id_t {iGlobalPlayerId}, coord});
		}
	}
}

void ParseFleetSync(std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& rRawPackets, std::vector<Fleet>& rOutFleets)
{
	for (auto it = rRawPackets.begin(); it != rRawPackets.end(); )
	{
		GamePacketType eType = static_cast<GamePacketType>(it->first);
		if (eType != GamePacketType::kServerFleetSync)
		{
			++it;
			continue;
		}

		const std::vector<uint8_t>& rPayload = it->second;
		const uint8_t* pCursor = rPayload.data();

		int64_t iFleetCount = engine::ReadInt64(pCursor);
		rOutFleets.resize(static_cast<size_t>(iFleetCount));
		for (int64_t i = 0; i < iFleetCount; ++i)
		{
			int64_t iMemberCount = engine::ReadInt64(pCursor);
			rOutFleets.at(static_cast<size_t>(i)).iFlagshipIndex = engine::ReadInt64(pCursor);
			rOutFleets.at(static_cast<size_t>(i)).fNavigationDelay = engine::ReadFloat(pCursor);
			rOutFleets.at(static_cast<size_t>(i)).members.resize(static_cast<size_t>(iMemberCount));
			for (int64_t j = 0; j < iMemberCount; ++j)
			{
				int64_t iGlobalPlayerId = engine::ReadInt64(pCursor);
				uint8_t uiAlive = engine::ReadUint8(pCursor);
				rOutFleets.at(static_cast<size_t>(i)).members.at(static_cast<size_t>(j)) = FleetMember {engine::global_id_t {iGlobalPlayerId}, uiAlive != 0};
			}
		}

		it = rRawPackets.erase(it);
	}
}

} // namespace game
