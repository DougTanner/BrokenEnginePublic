#include "Network/PlayerEvents.h"

#include "Network/NetworkCursor.h"
#include "Network/NetworkProtocol.h"

#include "Fleet.h"
#include "Game.h"
#include "Network/GamePacketType.h"

namespace game
{

void ParsePlayerEvents(std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& rRawPackets, common::ScopedWorkbufferArena& rOutEventsArena)
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
			rOutEventsArena.PushBack(ReceivedPlayerEvent{PlayerEventType::kAssigned, engine::global_id_t {iGlobalPlayerId}, coord});
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
			rOutEventsArena.PushBack(ReceivedPlayerEvent{eEventType, engine::global_id_t {iGlobalPlayerId}, coord});
		}
	}
}

// Network payload is a trust boundary: every read is bounded against the payload end and
// wire-supplied counts are validated before driving any resize. Returns false on a malformed
// payload; rOutFleets may be partially written on failure, so callers parse into a scratch
// vector and commit only on success.
static bool ParseFleetSyncPayload(const std::vector<uint8_t>& rPayload, std::vector<Fleet>& rOutFleets)
{
	engine::BoundedCursor cursor {rPayload.data(), rPayload.data() + rPayload.size()};

	// 8B fleet count
	if (!cursor.Has(8))
	{
		return false;
	}
	int64_t iFleetCount = engine::ReadInt64(cursor.pCursor);
	// Each fleet is at least 36 bytes: 16B guid + 8B memberCount + 8B flagshipIndex + 4B navigationDelay
	// (divide instead of multiply so a hostile count cannot overflow the bound check)
	if (iFleetCount < 0 || iFleetCount > cursor.Remaining() / 36)
	{
		return false;
	}
	rOutFleets.resize(static_cast<size_t>(iFleetCount));
	for (int64_t i = 0; i < iFleetCount; ++i)
	{
		// Members of earlier fleets consume payload, so the up-front bound is not sufficient per fleet
		if (!cursor.Has(36))
		{
			return false;
		}
		Fleet& rFleet = rOutFleets.at(static_cast<size_t>(i));
		rFleet.guid.uiHigh = engine::ReadUint64(cursor.pCursor);
		rFleet.guid.uiLow = engine::ReadUint64(cursor.pCursor);
		int64_t iMemberCount = engine::ReadInt64(cursor.pCursor);
		rFleet.iFlagshipIndex = engine::ReadInt64(cursor.pCursor);
		rFleet.fNavigationDelay = engine::ReadFloat(cursor.pCursor);
		// Each member is 9 bytes: 8B globalPlayerId + 1B alive
		if (iMemberCount < 0 || iMemberCount > cursor.Remaining() / 9)
		{
			return false;
		}
		rFleet.members.resize(static_cast<size_t>(iMemberCount));
		for (int64_t j = 0; j < iMemberCount; ++j)
		{
			int64_t iGlobalPlayerId = engine::ReadInt64(cursor.pCursor);
			uint8_t uiAlive = engine::ReadUint8(cursor.pCursor);
			rFleet.members.at(static_cast<size_t>(j)) = FleetMember {engine::global_id_t {iGlobalPlayerId}, uiAlive != 0};
		}
	}
	return true;
}

bool ParseFleetSync(std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& rRawPackets, std::vector<Fleet>& rOutFleets)
{
	// A valid sync (including a valid zero-fleet sync) commits into rOutFleets but leaves it empty when
	// fleetCount == 0, so emptiness cannot tell the caller "applied" from "nothing arrived" — report it explicitly
	bool bApplied = false;
	for (auto it = rRawPackets.begin(); it != rRawPackets.end(); )
	{
		GamePacketType eType = static_cast<GamePacketType>(it->first);
		if (eType != GamePacketType::kServerFleetSync)
		{
			++it;
			continue;
		}

		// Parse into a local list and commit only on success so a malformed sync is never
		// partially applied and cannot clobber a valid sync parsed earlier in this drain
		std::vector<Fleet> parsedFleets;
		if (ParseFleetSyncPayload(it->second, parsedFleets))
		{
			rOutFleets = std::move(parsedFleets);
			bApplied = true;
		}
		else
		{
			LOG(kNetwork, kWarning, "ParseFleetSync MalformedPayload Size: {}", it->second.size());
		}

		it = rRawPackets.erase(it);
	}

	return bApplied;
}

} // namespace game
