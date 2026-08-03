#pragma once

#if defined(BT_SERVER)

#include "Fleet.h"
#include "Network/Server/FleetNavigationController.h"

namespace game
{

struct ClientSpawnInfo;
struct OwnedPlayer;

struct PendingCreateFleetRequest
{
	int64_t iClientId = 0;
};

struct PendingDeleteFleetRequest
{
	int64_t iClientId = 0;
	FleetGuid fleetGuid {};
};

struct PendingSpawnIntoFleetRequest
{
	int64_t iClientId = 0;
	FleetGuid fleetGuid {};
};

struct PendingRespawnInFleetRequest
{
	int64_t iClientId = 0;
	FleetGuid fleetGuid {};
	int64_t iMemberIndex = 0;
};

class ServerFleetManager
{
public:

	ServerFleetManager();

	void ProcessCreateFleetRequests();
	void ProcessDeleteFleetRequests();
	void ProcessSpawnIntoFleetRequests();
	void ProcessRespawnInFleetRequests();
	void TickFleetTimers();
	void ProcessFlagshipUpdates();

	void SendFleetSyncToClient(int64_t iClientId, const engine::ClientGuid& rClientGuid);

	void QueueCreateRequest(const PendingCreateFleetRequest& rRequest);
	void QueueDeleteRequest(const PendingDeleteFleetRequest& rRequest);
	void QueueSpawnIntoRequest(const PendingSpawnIntoFleetRequest& rRequest);
	void QueueRespawnRequest(const PendingRespawnInFleetRequest& rRequest);
	void ClearPendingRequests();

	void OnPlayerDeath(const engine::ClientGuid& rGuid, engine::global_id_t globalId);
	void OnPlayerSpawned(int64_t iClientId, const engine::ClientGuid& rClientGuid, const ClientSpawnInfo& rSpawnInfo, engine::global_id_t globalPlayerId);
	void OnPlayerTransferred(const engine::ClientGuid& rGuid, engine::global_id_t globalPlayerId, engine::GridCoord destination);
	void OnClientConnected(int64_t iClientId, const engine::ClientGuid& rClientGuid);
	void OnClientDisconnected(const engine::ClientGuid& rClientGuid);
	void OnResetForLoad(int64_t iClientId, const engine::ClientGuid& rClientGuid);

	enum class FleetLookupFlags : uint8_t
	{
		kFound      = 1 << 0,
		kIsFlagship = 1 << 1,
	};
	using FleetLookupFlags_t = common::Flags<FleetLookupFlags>;

	struct FleetLookupResult
	{
		FleetLookupFlags_t flags {};
		engine::GridCoord fleetWantedCoord {};
		uint8_t uiPendingFleetWantedCoordTicks = 0;
	};
	FleetLookupResult LookupFleetWantedCoord(const engine::ClientGuid& rClientGuid, const FleetGuid& rFleetGuid, int64_t iMemberIndex);

	void UpdateFleetNavigationDelay(const engine::ClientGuid& rGuid, const FleetGuid& rFleetGuid, float fDelay);

	void DetectDisconnectedPlayerDeaths();

	// Destructive wipe of all fleet state (mFleets, mGuidToClientId, pending requests).
	// Use ONLY for fresh-game flows (ServerReset, kResetFrame). Do NOT call from the load path —
	// ReadFleetData is authoritative there; use ClearPendingRequests() instead.
	void ResetState();

	int64_t FindClientIdForGuid(const engine::ClientGuid& rGuid) const;

	// All fleets keyed by persistent ClientGuid (survives disconnect/reconnect)
	std::unordered_map<engine::ClientGuid, std::vector<Fleet>, engine::ClientGuidHash> mFleets;

	// Connected client mapping: ClientGuid -> iClientId (0 = disconnected)
	std::unordered_map<engine::ClientGuid, int64_t, engine::ClientGuidHash> mGuidToClientId;

	FleetNavigationController mNavigation;

	common::RandomEngine mRandomEngine;

private:

	// Position of rFleetGuid within one client's fleet vector; -1 when absent. Delete needs the position, not just the fleet.
	int64_t FindFleetIndexByGuid(const std::vector<Fleet>& rFleets, const FleetGuid& rFleetGuid) const;

	void RefreshFleetMembers(Fleet& rFleet, std::span<const OwnedPlayer> ownedPlayers);
	void ResetFleetForLoad(Fleet& rFleet, const engine::ClientGuid& rClientGuid);

	std::vector<PendingCreateFleetRequest> mPendingCreateFleetRequests;
	std::vector<PendingDeleteFleetRequest> mPendingDeleteFleetRequests;
	std::vector<PendingSpawnIntoFleetRequest> mPendingSpawnIntoFleetRequests;
	std::vector<PendingRespawnInFleetRequest> mPendingRespawnInFleetRequests;
};

} // namespace game

#endif // BT_SERVER
