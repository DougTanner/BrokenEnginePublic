#include "Agent/AgentCommands.h"

#if defined(BT_SERVER)

#include "Agent/AgentCommandsServerQueries.h"
#include "Game.h"
#include "Network/Server/ServerBroadcaster.h"
#include "Network/Server/ServerClientManager.h"

namespace game
{

namespace
{

// UTF-8 filesystem path -> UTF-8 std::string for JSON result echoing (no heap-narrow-conversion surprises).
std::string PathToUtf8(const std::filesystem::path& rPath)
{
	std::u8string u8String = rPath.u8string();
	return std::string(reinterpret_cast<const char*>(u8String.c_str()), u8String.size());
}

// Trust boundary: the agent-supplied save/load filename lands in the user's appdata directory. Reject anything
// but a bare filename (no path separators, no "..") so it can't escape that directory.
std::filesystem::path BareFilenameParam(const nlohmann::json& rValue)
{
	std::string utf8 = rValue.get<std::string>(); // throws on a non-string
	if (utf8.empty())
	{
		throw std::runtime_error("'file' must be a non-empty bare filename");
	}
	if (utf8.find('/') != std::string::npos || utf8.find('\\') != std::string::npos || utf8.find(':') != std::string::npos || utf8.find("..") != std::string::npos)
	{
		throw std::runtime_error("'file' must be a bare filename (no path separators, drive/stream ':', or '..')");
	}
	return std::filesystem::path(reinterpret_cast<const char8_t*>(utf8.c_str()));
}

void CommandStatus([[maybe_unused]] const nlohmann::json& rParams, nlohmann::json& rResult)
{
	rResult["tick"] = gpGame->TickCounter();
	rResult["paused"] = gpGame->mGameFlags & engine::GameFlags::kPaused;
	rResult["recording"] = gpGame->mGameSaveLoad.IsRecording();
	rResult["replaying"] = gpGame->mGameSaveLoad.IsReplaying();

	int64_t iClientCount = 0;
	for (const engine::ClientConnection& rClient : engine::gpServer->GetClients())
	{
		if (rClient.bHandshakeComplete)
		{
			++iClientCount;
		}
	}
	rResult["clientCount"] = iClientCount;

	nlohmann::json activeCoords = nlohmann::json::array();
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		activeCoords.push_back({rCoord.x, rCoord.y});
	}
	rResult["activeCoords"] = std::move(activeCoords);

	rResult["nextGlobalId"] = gpGame->NextGlobalId();
}

void CommandPause(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (!rParams.contains("paused") || !rParams.at("paused").is_boolean())
	{
		throw std::runtime_error("pause requires bool 'paused'");
	}
	bool bPaused = rParams.at("paused").get<bool>();
	gpGame->mGameFlags.Set(engine::GameFlags::kPaused, bPaused); // mirrors kClientPauseRequest
	rResult["paused"] = bPaused;
}

void CommandTimescale(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (!rParams.contains("faster") || !rParams.at("faster").is_boolean())
	{
		throw std::runtime_error("timescale requires bool 'faster'");
	}
	bool bFaster = rParams.at("faster").get<bool>();
	gpServerSession->StepTimescale(bFaster); // shared with kClientTimespeedRequest — steps + broadcasts to clients
	rResult["numerator"] = gpGame->mTimeStep.miTimeMultiply;
	rResult["denominator"] = gpGame->mTimeStep.miTimeDivide;
}

void CommandSave(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	// QuicksaveFile() overrides the GameBase virtual privately in game::Game, so read the default name via the base's
	// static type, then feed it to the path overload (identical to the no-arg ServerSave(), which forwards QuicksaveFile()).
	std::filesystem::path file = rParams.contains("file") ? BareFilenameParam(rParams.at("file")) : static_cast<engine::GameBase&>(*gpGame).QuicksaveFile();
	gpGame->mGameSaveLoad.ServerSave(file);
	rResult["file"] = PathToUtf8(file);
}

void CommandLoad(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	// See CommandSave: QuicksaveFile() is private on game::Game; read the default via the base ref, then use the path overload.
	std::filesystem::path file = rParams.contains("file") ? BareFilenameParam(rParams.at("file")) : static_cast<engine::GameBase&>(*gpGame).QuicksaveFile();

	bool bResetToFresh = false;
	if (!gpGame->mGameSaveLoad.ServerLoad(file))
	{
		// Corrupt/truncated save: ReadGrid already left a clean-slate grid, but ServerLoad's success tail never ran.
		// Fall back to a fresh game exactly like kClientLoadRequest rather than ticking a torn grid.
		gpGame->mGameSaveLoad.ServerReset();
		bResetToFresh = true;
	}
	rResult["file"] = PathToUtf8(file);
	rResult["resetToFresh"] = bResetToFresh;
}

void CommandReset([[maybe_unused]] const nlohmann::json& rParams, nlohmann::json& rResult)
{
	gpGame->mGameSaveLoad.ServerReset();
	rResult = nlohmann::json::object();
}

void CommandReplayRecord([[maybe_unused]] const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	// SaveLoadReplay/SyncReplayTick are compiled out under !kbDebugInput, so the flag would never be consumed.
	if constexpr (!kbDebugInput)
	{
		throw std::runtime_error("replay requires kbDebugInput build");
	}
	else
	{
		if (!rParams.contains("start") || !rParams.at("start").is_boolean())
		{
			throw std::runtime_error("replay_record requires bool 'start'");
		}
		bool bStart = rParams.at("start").get<bool>();
		// kSaveReplay is a pure toggle in SyncReplayTick (empty writer set starts, non-empty stops). While paused the
		// per-tick loop is skipped, so a set-but-unconsumed flag leaves the effective requested state the inverse of
		// IsRecording() — compute it, not IsRecording() alone. bEffective is the state the sim will settle into once the
		// pending flag (if any) is consumed. If that already matches the request, no change is pending; otherwise flip the
		// toggle: setting a clear flag schedules a transition, clearing a set flag cancels a not-yet-consumed one.
		bool bFlagPending = gpGame->mGameFlags & engine::GameFlags::kSaveReplay;
		bool bEffective = gpGame->mGameSaveLoad.IsRecording() != bFlagPending;
		if (bStart == bEffective)
		{
			rResult["pending"] = false;
		}
		else if (bFlagPending)
		{
			// Cancel a pending transition (e.g. a stop request voids a not-yet-started recording).
			gpGame->mGameFlags.Clear(engine::GameFlags::kSaveReplay);
			rResult["pending"] = false;
		}
		else
		{
			gpGame->mGameFlags.Set(engine::GameFlags::kSaveReplay);
			rResult["pending"] = true;
		}
	}
}

void CommandReplayPlay([[maybe_unused]] const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	if constexpr (!kbDebugInput)
	{
		throw std::runtime_error("replay requires kbDebugInput build");
	}
	else
	{
		// Same semantics as F8 / kClientReplayPlaybackRequest: starts playback, or cancels if already replaying.
		gpGame->mGameFlags.Set(engine::GameFlags::kLoadReplay);
		rResult["pending"] = true;
	}
}

} // namespace

// ---- Shared helper (trust boundary — params are external input) ----

// Parse a [x,y] JSON array into a GridCoord; .get<int32_t>() throws on a non-number.
engine::GridCoord CoordFromParam(const nlohmann::json& rParams, const char* pcKey)
{
	if (!rParams.contains(pcKey) || !rParams.at(pcKey).is_array() || rParams.at(pcKey).size() != 2)
	{
		throw std::runtime_error(std::string("'") + pcKey + "' must be a [x,y] array");
	}
	const nlohmann::json& rCoord = rParams.at(pcKey);
	return engine::GridCoord {rCoord.at(0).get<int32_t>(), rCoord.at(1).get<int32_t>()};
}

namespace
{

// ---- StatusChange injection helpers ----

bool IsCoordActive(engine::GridCoord coord)
{
	return std::find(gpGame->mActiveCoords.begin(), gpGame->mActiveCoords.end(), coord) != gpGame->mActiveCoords.end();
}

int64_t PlayerUuidFromParam(const nlohmann::json& rChange)
{
	if (!rChange.contains("playerUuid"))
	{
		throw std::runtime_error("'playerUuid' required");
	}
	return rChange.at("playerUuid").get<int64_t>();
}

// Mirrors the wire clamp: non-finite → 60, then clamp to [0,60].
float NavigationDelayFromParam(const nlohmann::json& rChange)
{
	float fDelay = rChange.contains("navigationDelay") ? rChange.at("navigationDelay").get<float>() : 60.0f;
	if (!std::isfinite(fDelay))
	{
		fDelay = 60.0f;
	}
	return std::clamp(fDelay, 0.0f, 60.0f);
}

// Build one injectable StatusChange from a change entry, minting a global id at inject-time for SpawnPlayer
// (pushed into rGlobalIds). Only SpawnPlayer/DestroyPlayer/UpdatePlayer/UpdateFleet accepted; others rejected.
std::pair<engine::GridCoord, StatusChange> BuildInjectedChange(const nlohmann::json& rChange, nlohmann::json& rGlobalIds)
{
	engine::GridCoord coord = CoordFromParam(rChange);
	if (!IsCoordActive(coord))
	{
		throw std::runtime_error("change 'coord' is not active");
	}
	if (!rChange.contains("type") || !rChange.at("type").is_string())
	{
		throw std::runtime_error("each change requires string 'type'");
	}
	std::string type = rChange.at("type").get<std::string>();

	StatusChange change;
	if (type == "SpawnPlayer")
	{
		int64_t iGlobalId = gpGame->GenerateGlobalId();
		bool bIsFlagship = rChange.contains("isFlagship") && rChange.at("isFlagship").get<bool>();
		engine::GridCoord fleetWantedCoord = rChange.contains("fleetWantedCoord") ? CoordFromParam(rChange, "fleetWantedCoord") : coord;
		change.eType = StatusChangeType::kSpawnPlayer;
		change.data = SpawnPlayerData {.iGlobalId = iGlobalId, .bIsFlagship = bIsFlagship, .fleetWantedCoord = fleetWantedCoord, .uiPendingFleetWantedCoordTicks = 0};
		rGlobalIds.push_back(iGlobalId);
	}
	else if (type == "DestroyPlayer")
	{
		change.eType = StatusChangeType::kDestroyPlayer;
		change.data = DestroyPlayerData {.iPlayerUuid = PlayerUuidFromParam(rChange)};
	}
	else if (type == "UpdatePlayer")
	{
		bool bUseMissiles = rChange.contains("useMissiles") && rChange.at("useMissiles").get<bool>();
		change.eType = StatusChangeType::kUpdatePlayer;
		change.data = UpdatePlayerData {.iPlayerUuid = PlayerUuidFromParam(rChange), .bUseMissiles = bUseMissiles, .fNavigationDelay = NavigationDelayFromParam(rChange), .uiPendingWeaponModeTicks = static_cast<uint8_t>(engine::kiTickRate)};
	}
	else if (type == "UpdateFleet")
	{
		bool bIsFlagship = rChange.contains("isFlagship") && rChange.at("isFlagship").get<bool>();
		change.eType = StatusChangeType::kUpdateFleet;
		change.data = UpdateFleetData {.iPlayerUuid = PlayerUuidFromParam(rChange), .bIsFlagship = bIsFlagship, .fleetWantedCoord = CoordFromParam(rChange, "fleetWantedCoord"), .uiPendingFleetWantedCoordTicks = static_cast<uint8_t>(engine::kiTickRate)};
	}
	else
	{
		throw std::runtime_error("'type' must be SpawnPlayer|DestroyPlayer|UpdatePlayer|UpdateFleet");
	}
	return {coord, change};
}

// True while a client sits in mClientsWaitingForSpawn: the spawn-assignment-by-snapshot-diff invariant would
// mis-assign an agent SpawnPlayer landing the same tick to the waiting client — reject injection outright.
bool ClientsWaitingForSpawn()
{
	return !gpServerSession->mpClientManager->mClientsWaitingForSpawn.empty();
}

void CommandInjectStatusChanges(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	// Replay playback resimulates from the recorded stream: SyncReplayTick's LoadDifference overwrites mFrameInputs,
	// so an injection would be silently lost (or broadcast-but-not-simulated → desync). Reject rather than mislead.
	if (gpGame->mGameSaveLoad.IsReplaying())
	{
		throw std::runtime_error("cannot inject during replay playback");
	}
	if (ClientsWaitingForSpawn())
	{
		throw std::runtime_error("cannot inject while clients are waiting for spawn");
	}
	if (!rParams.contains("changes") || !rParams.at("changes").is_array())
	{
		throw std::runtime_error("inject_status_changes requires array 'changes'");
	}
	const nlohmann::json& rChanges = rParams.at("changes");

	// Build everything first (may throw) so a malformed entry can't partially apply — nothing is queued until all
	// entries validate. Burned global ids on a mid-build throw are harmless (ids are monotonic; gaps are fine).
	nlohmann::json globalIds = nlohmann::json::array();
	std::vector<std::pair<engine::GridCoord, StatusChange>> built;
	for (const nlohmann::json& rChange : rChanges)
	{
		built.push_back(BuildInjectedChange(rChange, globalIds));
	}
	for (const auto& [rCoord, rChange] : built)
	{
		gpServerSession->mpBroadcaster->QueueAgentStatusChange(rCoord, rChange);
	}

	rResult["injected"] = std::ssize(built);
	rResult["globalIds"] = std::move(globalIds);
	rResult["deferred"] = static_cast<bool>(gpGame->mGameFlags & engine::GameFlags::kPaused);
}

void CommandSpawnPlayers(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (gpGame->mGameSaveLoad.IsReplaying())
	{
		throw std::runtime_error("cannot inject during replay playback");
	}
	if (ClientsWaitingForSpawn())
	{
		throw std::runtime_error("cannot inject while clients are waiting for spawn");
	}
	engine::GridCoord coord = CoordFromParam(rParams);
	if (!IsCoordActive(coord))
	{
		throw std::runtime_error("'coord' is not active");
	}
	if (!rParams.contains("count"))
	{
		throw std::runtime_error("spawn_players requires 'count'");
	}
	int64_t iCount = rParams.at("count").get<int64_t>();
	// Cap: each id minted here is an unbounded up-front allocation; a huge count would OOM before any tick runs.
	constexpr int64_t kiMaxSpawnCount = 256; // matches the query window default limit
	if (iCount < 0 || iCount > kiMaxSpawnCount)
	{
		throw std::runtime_error("'count' must be in [0, 256]");
	}
	bool bIsFlagship = rParams.contains("isFlagship") && rParams.at("isFlagship").get<bool>();

	nlohmann::json globalIds = nlohmann::json::array();
	for (int64_t i = 0; i < iCount; ++i)
	{
		int64_t iGlobalId = gpGame->GenerateGlobalId();
		StatusChange change {.eType = StatusChangeType::kSpawnPlayer, .data = SpawnPlayerData {.iGlobalId = iGlobalId, .bIsFlagship = bIsFlagship, .fleetWantedCoord = coord, .uiPendingFleetWantedCoordTicks = 0}};
		gpServerSession->mpBroadcaster->QueueAgentStatusChange(coord, change);
		globalIds.push_back(iGlobalId);
	}

	rResult["injected"] = iCount;
	rResult["globalIds"] = std::move(globalIds);
	rResult["deferred"] = static_cast<bool>(gpGame->mGameFlags & engine::GameFlags::kPaused);
}

} // namespace

bool ExecuteAgentCommandServer(std::string_view cmd, const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (cmd == "status")
	{
		CommandStatus(rParams, rResult);
		return true;
	}
	if (cmd == "pause")
	{
		CommandPause(rParams, rResult);
		return true;
	}
	if (cmd == "timescale")
	{
		CommandTimescale(rParams, rResult);
		return true;
	}
	if (cmd == "save")
	{
		CommandSave(rParams, rResult);
		return true;
	}
	if (cmd == "load")
	{
		CommandLoad(rParams, rResult);
		return true;
	}
	if (cmd == "reset")
	{
		CommandReset(rParams, rResult);
		return true;
	}
	if (cmd == "replay_record")
	{
		CommandReplayRecord(rParams, rResult);
		return true;
	}
	if (cmd == "replay_play")
	{
		CommandReplayPlay(rParams, rResult);
		return true;
	}
	if (cmd == "query_frame")
	{
		CommandQueryFrame(rParams, rResult);
		return true;
	}
	if (cmd == "query_players")
	{
		CommandQueryPlayers(rParams, rResult);
		return true;
	}
	if (cmd == "query_collection")
	{
		CommandQueryCollection(rParams, rResult);
		return true;
	}
	if (cmd == "inject_status_changes")
	{
		CommandInjectStatusChanges(rParams, rResult);
		return true;
	}
	if (cmd == "spawn_players")
	{
		CommandSpawnPlayers(rParams, rResult);
		return true;
	}
	return false;
}

} // namespace game

#endif // defined(BT_SERVER)
