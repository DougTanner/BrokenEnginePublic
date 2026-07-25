#include "Pch.h"

#include "Network/PlayerEvents.h"

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
			if (rPayload.size() != static_cast<size_t>(GameMessages::AssignPlayerMessage::kiSize))
			{
				continue;
			}
			GameMessages::AssignPlayerMessage message {};
			if (!engine::NetworkMessages::Read(rPayload, message))
			{
				continue;
			}
			rOutEventsArena.PushBack(ReceivedPlayerEvent{PlayerEventType::kAssigned, engine::global_id_t {message.iGlobalPlayerId}, message.coord});
		}
		else if (eType == GamePacketType::kServerPlayerState)
		{
			if (rPayload.size() != static_cast<size_t>(GameMessages::PlayerStateMessage::kiSize))
			{
				continue;
			}
			GameMessages::PlayerStateMessage message {};
			if (!engine::NetworkMessages::Read(rPayload, message))
			{
				continue;
			}
			const GameMessages::PlayerStateDescriptor* pDescriptor = GameMessages::FindPlayerStateDescriptor(message.uiWireType);
			if (pDescriptor == nullptr)
			{
				continue;
			}
			rOutEventsArena.PushBack(ReceivedPlayerEvent{pDescriptor->eEventType, engine::global_id_t {message.iGlobalPlayerId}, message.coord});
		}
	}
}

// Network payload is a trust boundary: every read is bounded against the payload end and
// wire-supplied counts are validated before driving any resize. Returns false on a malformed
// payload; rOutFleets may be partially written on failure, so callers parse into a scratch
// vector and commit only on success.
static bool ParseFleetSyncPayload(const std::vector<uint8_t>& rPayload, std::vector<Fleet>& rOutFleets)
{
	return GameMessages::FleetSyncMessage::ReadPayload(rPayload, rOutFleets);
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
