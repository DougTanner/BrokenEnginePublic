#include "Agent/AgentCommands.h"

#if defined(BT_SERVER)

#include "Agent/AgentCommandsServerQueries.h"
#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/Server/ServerBroadcaster.h"
#include "Network/Server/ServerClientManager.h"
#include "Network/Server/ServerFleetManager.h"
#include "Network/Server/ServerTransferManager.h"
#include "Profile/ProfileManager.h"

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

bool IsWindowsReservedDeviceBasename(std::string_view utf8)
{
	std::string basename(utf8.substr(0, utf8.find('.')));
	// Win32 strips trailing spaces and dots from a final path component, so "NUL " or "NUL ." still
	// resolves to the device.
	while (!basename.empty() && (basename.back() == ' ' || basename.back() == '.'))
	{
		basename.pop_back();
	}
	for (char& rCharacter : basename)
	{
		if (rCharacter >= 'a' && rCharacter <= 'z')
		{
			rCharacter = static_cast<char>(rCharacter - ('a' - 'A'));
		}
	}

	if (basename == "CON" || basename == "NUL" || basename == "PRN" || basename == "AUX")
	{
		return true;
	}

	return basename.size() == 4 &&
		(basename.starts_with("COM") || basename.starts_with("LPT")) &&
		basename[3] >= '1' && basename[3] <= '9';
}

// Trust boundary: the agent-supplied save/load filename lands in the user's appdata directory. Reject anything
// but a bare filename (no path separators, no "..") and Windows reserved device basenames.
std::filesystem::path BareFilenameParam(const nlohmann::json& rValue)
{
	std::string utf8 = rValue.get<std::string>(); // throws on a non-string
	if (utf8.empty())
	{
		throw std::runtime_error("'file' must be a non-empty bare filename");
	}
	if (utf8.find('\0') != std::string::npos)
	{
		throw std::runtime_error("'file' must not contain an embedded NUL");
	}
	if (utf8.find('/') != std::string::npos || utf8.find('\\') != std::string::npos || utf8.find(':') != std::string::npos || utf8.find("..") != std::string::npos)
	{
		throw std::runtime_error("'file' must be a bare filename (no path separators, drive/stream ':', or '..')");
	}
	if (IsWindowsReservedDeviceBasename(utf8))
	{
		throw std::runtime_error("'file' must not use a reserved Windows device name");
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
	for (const engine::ClientConnection& rClient : engine::gpServer->mClients)
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
	rResult["pendingFlagshipUpdateCount"] = std::ssize(gpServerSession->mpFleetManager->mNavigation.mPendingFlagshipUpdates);
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
	std::filesystem::path file = rParams.contains("file") ? BareFilenameParam(rParams.at("file")) : gpGame->QuicksaveFile();
	if (!gpGame->mGameSaveLoad.ServerSave(file))
	{
		throw std::runtime_error("save failed to write '" + PathToUtf8(file) + "'");
	}
	rResult["file"] = PathToUtf8(file);
}

void CommandLoad(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (rParams.contains("pauseAfterLoad") && !rParams.at("pauseAfterLoad").is_boolean())
	{
		throw std::runtime_error("load requires bool 'pauseAfterLoad'");
	}
	bool bPauseAfterLoad = rParams.value("pauseAfterLoad", false);
	std::filesystem::path file = rParams.contains("file") ? BareFilenameParam(rParams.at("file")) : gpGame->QuicksaveFile();

	bool bResetToFresh = false;
	if (!gpGame->mGameSaveLoad.ServerLoad(file))
	{
		// Corrupt/truncated save: ReadGrid already left a clean-slate grid, but ServerLoad's success tail never ran.
		// Fall back to a fresh game exactly like kClientLoadRequest rather than ticking a torn grid.
		gpGame->mGameSaveLoad.ServerReset();
		bResetToFresh = true;
	}
	if (bPauseAfterLoad)
	{
		gpGame->mGameFlags.Set(engine::GameFlags::kPaused);
	}
	rResult["file"] = PathToUtf8(file);
	rResult["resetToFresh"] = bResetToFresh;
	rResult["paused"] = static_cast<bool>(gpGame->mGameFlags & engine::GameFlags::kPaused);
	rResult["pendingFlagshipUpdateCount"] = std::ssize(gpServerSession->mpFleetManager->mNavigation.mPendingFlagshipUpdates);
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
			if (!gpGame->mGameSaveLoad.IsRecording())
			{
				gpServerSession->mpTransferManager->mReplayTransferFixtures.clear();
				gpGame->mGameSaveLoad.mReplayTransferCaptureInfo = {};
			}
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

void CommandReplayTransferCapture([[maybe_unused]] const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if constexpr (!kbDebugInput)
	{
		throw std::runtime_error("replay_transfer_capture requires kbDebugInput build");
	}
	else
	{
		const GameSaveLoad::ReplayTransferCaptureInfo& rCaptureInfo = gpGame->mGameSaveLoad.mReplayTransferCaptureInfo;
		rResult["recordingEventTick"] = rCaptureInfo.iRecordingEventTick;
		rResult["playbackEventTick"] = rCaptureInfo.iPlaybackEventTick;
		rResult["writerInputTick"] = rCaptureInfo.iWriterInputTick;
		rResult["followingEmptyInputTick"] = rCaptureInfo.iFollowingEmptyInputTick;
		rResult["firstWriterInputTick"] = rCaptureInfo.iFirstWriterInputTick;
		rResult["writerInputCount"] = rCaptureInfo.iWriterInputCount;
		rResult["playerCount"] = rCaptureInfo.iPlayerCount;
		rResult["spaceshipCount"] = rCaptureInfo.iSpaceshipCount;
		rResult["blasterCount"] = rCaptureInfo.iBlasterCount;
		rResult["missileCount"] = rCaptureInfo.iMissileCount;
	}
}

void CommandReplayDropRetainedEndFrame([[maybe_unused]] const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	if constexpr (!kbDebugInput)
	{
		throw std::runtime_error("replay requires kbDebugInput build");
	}
	else
	{
		if (!gpGame->mGameSaveLoad.IsRecording())
		{
			throw std::runtime_error("replay_drop_retained_end_frame requires active recording");
		}

		engine::GridCoord coord = CoordFromParam(rParams);
		if (!gpGame->mGameSaveLoad.DropRetainedReplayEndFrame(coord))
		{
			throw std::runtime_error("replay_drop_retained_end_frame requires a retained terminal frame for 'coord'");
		}

		rResult["coord"] = {coord.x, coord.y};
		rResult["dropped"] = true;
	}
}

void CommandReplayInjectPersistenceFailure([[maybe_unused]] const nlohmann::json& rParams, [[maybe_unused]] nlohmann::json& rResult)
{
	if constexpr (!kbDebugInput)
	{
		throw std::runtime_error("replay requires kbDebugInput build");
	}
	else
	{
		if (!rParams.contains("stage") || !rParams.at("stage").is_string())
		{
			throw std::runtime_error("replay_inject_persistence_failure requires string 'stage'");
		}

		const std::string stage = rParams.at("stage").get<std::string>();
		GameSaveLoad::ReplayPersistenceFailurePoint eFailurePoint = GameSaveLoad::ReplayPersistenceFailurePoint::kNone;
		if (stage == "invalidation")
		{
			eFailurePoint = GameSaveLoad::ReplayPersistenceFailurePoint::kManifestInvalidation;
		}
		else if (stage == "grid")
		{
			eFailurePoint = GameSaveLoad::ReplayPersistenceFailurePoint::kGrid;
		}
		else if (stage == "coordinate_writer")
		{
			eFailurePoint = GameSaveLoad::ReplayPersistenceFailurePoint::kCoordinateWriter;
		}
		else if (stage == "metadata")
		{
			eFailurePoint = GameSaveLoad::ReplayPersistenceFailurePoint::kMetadata;
		}
		else if (stage == "inventory")
		{
			eFailurePoint = GameSaveLoad::ReplayPersistenceFailurePoint::kInventory;
		}
		else if (stage == "final_manifest")
		{
			eFailurePoint = GameSaveLoad::ReplayPersistenceFailurePoint::kFinalManifest;
		}
		else
		{
			throw std::runtime_error("'stage' must be invalidation|grid|coordinate_writer|metadata|inventory|final_manifest");
		}

		if ((eFailurePoint == GameSaveLoad::ReplayPersistenceFailurePoint::kManifestInvalidation ||
			eFailurePoint == GameSaveLoad::ReplayPersistenceFailurePoint::kGrid) == gpGame->mGameSaveLoad.IsRecording())
		{
			throw std::runtime_error(gpGame->mGameSaveLoad.IsRecording() ? "selected stage requires recording to be inactive" : "selected stage requires active recording");
		}

		if ((eFailurePoint == GameSaveLoad::ReplayPersistenceFailurePoint::kCoordinateWriter) != rParams.contains("coord"))
		{
			throw std::runtime_error(eFailurePoint == GameSaveLoad::ReplayPersistenceFailurePoint::kCoordinateWriter ? "coordinate_writer requires 'coord'" : "'coord' is only valid for coordinate_writer");
		}

		engine::GridCoord coord {};
		if (eFailurePoint == GameSaveLoad::ReplayPersistenceFailurePoint::kCoordinateWriter)
		{
			coord = CoordFromParam(rParams);
		}
		if (!gpGame->mGameSaveLoad.ArmReplayPersistenceFailure(eFailurePoint, coord))
		{
			throw std::runtime_error("coordinate_writer 'coord' has no replay writer with an end frame");
		}

		rResult["stage"] = stage;
		if (eFailurePoint == GameSaveLoad::ReplayPersistenceFailurePoint::kCoordinateWriter)
		{
			rResult["coord"] = {coord.x, coord.y};
		}
		rResult["armed"] = true;
	}
}

void CommandQueryProfile(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if (!rParams.is_object())
	{
		throw std::runtime_error("query_profile params must be an object");
	}

	bool bAcknowledgementRequested = false;
	uint64_t uiAcknowledgementSequence = 0;
	for (const auto& [rKey, rValue] : rParams.items())
	{
		if (rKey != "ackActivationEventSequence")
		{
			throw std::runtime_error("query_profile unknown parameter '" + rKey + "'");
		}
		if (!rValue.is_number_unsigned() || rValue.get<uint64_t>() == 0)
		{
			throw std::runtime_error("'ackActivationEventSequence' must be a nonzero unsigned integer");
		}
		bAcknowledgementRequested = true;
		uiAcknowledgementSequence = rValue.get<uint64_t>();
	}
	if constexpr (!kbProfiling)
	{
		if (bAcknowledgementRequested)
		{
			throw std::runtime_error("ackActivationEventSequence requires a profiling server");
		}
	}

	nlohmann::json timers = nlohmann::json::array();
	{
		std::lock_guard lock(gpProfileManager->mCpuTimerMutex);
		bool bActivationEventAcknowledged = false;
		if constexpr (kbProfiling)
		{
			if (bAcknowledgementRequested)
			{
				bActivationEventAcknowledged = gpProfileManager->AcknowledgeRawCpuTimerEvent(game::kCpuTimerPostRenderUpdateNavQuery, uiAcknowledgementSequence);
			}
		}

		for (int64_t i = 0; i < gpProfileManager->GetCpuTimerCount(); ++i)
		{
			engine::CpuTimer& rTimer = gpProfileManager->GetCpuTimer(i);
			nlohmann::json timer;
			timer["index"] = i;
			timer["name"] = std::string(gpProfileManager->GetCpuTimerName(i));
			timer["currentUs"] = rTimer.smoothedMicroseconds.Current();
			timer["averageUs"] = rTimer.smoothedMicroseconds.Average();
			timer["maxUs"] = rTimer.smoothedMicroseconds.Max();
			timer["allocations"] = rTimer.smoothedAllocations.Current();
			timer["threads"] = rTimer.iThreads;
			if constexpr (kbProfiling)
			{
				if (i == game::kCpuTimerPostRenderUpdateNavQuery)
				{
					engine::RawCpuTimerRecord rawRecord = gpProfileManager->GetRawCpuTimer(i);
					timer["sampleSequence"] = rawRecord.uiSampleSequence;
					timer["sampleUs"] = rawRecord.iSampleUs;
					timer["queryCount"] = rawRecord.iInvocationCount;
					timer["aStarCount"] = rawRecord.iAuxiliaryCount;

					engine::RawCpuTimerEventRecord eventRecord = gpProfileManager->GetRawCpuTimerEvent(i);
					const bool bEventAvailable = eventRecord.flags & engine::RawCpuTimerEventFlags::kAvailable;
					const bool bEventOverrun = eventRecord.flags & engine::RawCpuTimerEventFlags::kOverrun;
					timer["activationEvent"] = {
						{"available", bEventAvailable},
						{"eventSequence", eventRecord.uiEventSequence},
						{"sampleSequence", eventRecord.uiSampleSequence},
						{"sampleTick", eventRecord.iSampleTick},
						{"sampleUs", eventRecord.iSampleUs},
						{"queryCount", eventRecord.iInvocationCount},
						{"aStarCount", eventRecord.iAuxiliaryCount},
						{"qualifying", bEventAvailable && eventRecord.iInvocationCount == 8 && eventRecord.iAuxiliaryCount == 8},
						{"overrun", bEventOverrun},
					};
					if (bAcknowledgementRequested)
					{
						timer["activationEventAcknowledged"] = bActivationEventAcknowledged;
					}
				}
			}
			timers.push_back(std::move(timer));
		}
	}
	rResult["timers"] = std::move(timers);

	nlohmann::json counters = nlohmann::json::array();
	for (int64_t i = 0; i < gpProfileManager->GetCpuCounterCount(); ++i)
	{
		engine::CpuCounter& rCounter = gpProfileManager->GetCpuCounter(i);
		nlohmann::json counter;
		counter["index"] = i;
		counter["name"] = std::string(gpProfileManager->GetCpuCounterName(i));
		counter["count"] = rCounter.iCount;
		counters.push_back(std::move(counter));
	}
	rResult["counters"] = std::move(counters);
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

bool AreAdjacent(engine::GridCoord source, engine::GridCoord destination)
{
	const int64_t iDeltaX = static_cast<int64_t>(destination.x) - source.x;
	const int64_t iDeltaY = static_cast<int64_t>(destination.y) - source.y;
	return (iDeltaX != 0 || iDeltaY != 0) && std::abs(iDeltaX) <= 1 && std::abs(iDeltaY) <= 1;
}

void CommandReplayTransferFixture(const nlohmann::json& rParams, nlohmann::json& rResult)
{
	if constexpr (!kbDebugInput)
	{
		throw std::runtime_error("replay_transfer_fixture requires kbDebugInput build");
	}
	else
	{
		if (gpGame->mGameSaveLoad.IsReplaying())
		{
			throw std::runtime_error("cannot queue replay transfer fixture during replay playback");
		}
		const bool bPendingStart = (gpGame->mGameFlags & engine::GameFlags::kPaused) &&
			(gpGame->mGameFlags & engine::GameFlags::kSaveReplay) && !gpGame->mGameSaveLoad.IsRecording();
		if (!gpGame->mGameSaveLoad.IsRecording() && !bPendingStart)
		{
			throw std::runtime_error("replay_transfer_fixture requires active recording or a paused pending recording start");
		}
		if (!rParams.contains("type") || !rParams.at("type").is_string())
		{
			throw std::runtime_error("replay_transfer_fixture requires string 'type'");
		}
		bool bPauseAfterWriterInput = false;
		if (rParams.contains("pauseAfterWriterInput"))
		{
			if (!rParams.at("pauseAfterWriterInput").is_boolean())
			{
				throw std::runtime_error("'pauseAfterWriterInput' must be bool");
			}
			bPauseAfterWriterInput = rParams.at("pauseAfterWriterInput").get<bool>();
		}
		if (bPauseAfterWriterInput && gpGame->mGameSaveLoad.mReplayTransferCaptureInfo.iPauseAfterWriterInputCount != -1)
		{
			throw std::runtime_error("replay_transfer_fixture pauseAfterWriterInput is already armed");
		}

		const std::string type = rParams.at("type").get<std::string>();
		StatusChangeType eType {};
		if (type == "player")
		{
			eType = StatusChangeType::kTransferPlayer;
		}
		else if (type == "spaceship")
		{
			eType = StatusChangeType::kTransferSpaceship;
		}
		else if (type == "blaster")
		{
			eType = StatusChangeType::kTransferBlaster;
		}
		else if (type == "missile")
		{
			eType = StatusChangeType::kTransferMissile;
		}
		else
		{
			throw std::runtime_error("'type' must be player|spaceship|blaster|missile");
		}

		const engine::GridCoord source = CoordFromParam(rParams, "source");
		const engine::GridCoord destination = CoordFromParam(rParams, "destination");
		if (!AreAdjacent(source, destination))
		{
			throw std::runtime_error("'source' and 'destination' must be distinct adjacent coords");
		}
		if (!IsCoordActive(source))
		{
			throw std::runtime_error("'source' is not active");
		}
		const auto sourceIt = gpGame->mCoordFrames.find(source);
		if (sourceIt == gpGame->mCoordFrames.end() || sourceIt->second.pCurrent == nullptr || sourceIt->second.pNext == nullptr)
		{
			throw std::runtime_error("'source' frame is not ready");
		}

		XMVECTOR vecPosition = XMVectorSet(static_cast<float>(destination.x) * Frame::kfCellWidth, static_cast<float>(destination.y) * Frame::kfCellHeight, engine::gBaseHeight.Get(), 1.0f);
		if (eType == StatusChangeType::kTransferBlaster)
		{
			auto destinationIt = gpGame->mCoordFrames.find(destination);
			if (destinationIt == gpGame->mCoordFrames.end())
			{
				throw std::runtime_error("replay transfer fixture destination frame is not ready");
			}

			engine::FrameStaticData& rDestinationStaticData = destinationIt->second.staticData;
			if (rDestinationStaticData.elevationGrid.empty() && !rDestinationStaticData.islands.empty())
			{
				// Heap: Build the one-time derived terrain grid before this command samples it.
				ScopedSuppressAllocationTracking suppress;
				engine::gpIslandTerrain->BuildElevationGrid(rDestinationStaticData.coord, rDestinationStaticData.islands, rDestinationStaticData.elevationGrid);
			}
			XMFLOAT4A f4Area {};
			XMStoreFloat4A(&f4Area, rDestinationStaticData.vecArea);
			// Blasters are destroyed by point-terrain contact, so place this debug fixture in a terrain-clear
			// cell and verify the first fixed-tick movement remains clear too.
			constexpr int64_t kiTerrainGridDim = 20;
			float fPitchX = (f4Area.z - f4Area.x) / static_cast<float>(kiTerrainGridDim);
			float fPitchY = (f4Area.y - f4Area.w) / static_cast<float>(kiTerrainGridDim);
			bool bFoundTerrainClearPosition = false;
			for (int64_t iGridY = 0; iGridY < kiTerrainGridDim && !bFoundTerrainClearPosition; ++iGridY)
			{
				for (int64_t iGridX = 0; iGridX < kiTerrainGridDim; ++iGridX)
				{
					XMVECTOR vecCandidate = XMVectorSet(f4Area.x + (static_cast<float>(iGridX) + 0.5f) * fPitchX, f4Area.w + (static_cast<float>(iGridY) + 0.5f) * fPitchY, engine::gBaseHeight.Get(), 1.0f);
					XMVECTOR vecNextCandidate = XMVectorSet(XMVectorGetX(vecCandidate) + kfDeltaTime, XMVectorGetY(vecCandidate), engine::gBaseHeight.Get(), 1.0f);
					if (engine::gpIslandTerrain->FrameElevation(rDestinationStaticData, vecCandidate) < engine::gBaseHeight.Get() &&
						engine::gpIslandTerrain->FrameElevation(rDestinationStaticData, vecNextCandidate) < engine::gBaseHeight.Get())
					{
						vecPosition = vecCandidate;
						bFoundTerrainClearPosition = true;
						break;
					}
				}
			}
			if (!bFoundTerrainClearPosition)
			{
				throw std::runtime_error("replay transfer fixture destination has no terrain-clear Blaster position");
			}
		}

		TransferData data {
			.vecPosition = vecPosition,
			.vecDirection = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
			.vecVelocity = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
			.alignment = gpGame->PlayerAlignment(),
			.fHealth = 1.0f,
			.fShield = 1.0f,
			.uiTypeIndex = PlayersInterpolate::suiBlasterTypeIndex,
			.fDeltaRotationMax = eType == StatusChangeType::kTransferMissile ? 2.0f : 0.0f,
			.globalPlayerId = engine::global_id_t {eType == StatusChangeType::kTransferPlayer ? gpGame->GenerateGlobalId() : 0},
			.fleetWantedCoord = destination,
		};
		StatusChange transfer {.eType = eType, .data = std::move(data)};
		if (!gpServerSession->mpTransferManager->QueueReplayTransferFixture(destination, std::move(transfer)))
		{
			throw std::runtime_error("replay transfer fixture destination is not live for this type");
		}
		if (bPauseAfterWriterInput)
		{
			gpGame->mGameSaveLoad.mReplayTransferCaptureInfo.iPauseAfterWriterInputCount = bPendingStart ? 1 :
				gpGame->mGameSaveLoad.mReplayTransferCaptureInfo.iWriterInputCount + 2;
		}

		rResult["type"] = type;
		rResult["source"] = {source.x, source.y};
		rResult["destination"] = {destination.x, destination.y};
		rResult["pauseAfterWriterInput"] = bPauseAfterWriterInput;
	}
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
	if (!rParams.is_object())
	{
		throw std::runtime_error("inject_status_changes params must be an object");
	}
	for (const auto& [rKey, rValue] : rParams.items())
	{
		if (rKey != "changes" && rKey != "navQueryActivation")
		{
			throw std::runtime_error("inject_status_changes unknown parameter '" + rKey + "'");
		}
	}
	if (!rParams.contains("changes") || !rParams.at("changes").is_array())
	{
		throw std::runtime_error("inject_status_changes requires array 'changes'");
	}

	bool bArmNavQuery = false;
	if (rParams.contains("navQueryActivation"))
	{
		const nlohmann::json& rActivation = rParams.at("navQueryActivation");
		if (!rActivation.is_object())
		{
			throw std::runtime_error("'navQueryActivation' must be an object");
		}
		for (const auto& [rKey, rValue] : rActivation.items())
		{
			if (rKey != "arm")
			{
				throw std::runtime_error("navQueryActivation unknown parameter '" + rKey + "'");
			}
		}
		if (!rActivation.contains("arm") || !rActivation.at("arm").is_boolean() || !rActivation.at("arm").get<bool>())
		{
			throw std::runtime_error("navQueryActivation requires boolean 'arm': true");
		}
		bArmNavQuery = true;
	}

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
	if (bArmNavQuery)
	{
		if constexpr (!kbProfiling)
		{
			throw std::runtime_error("navQueryActivation requires a profiling server");
		}
		else if ((gpGame->mGameFlags & engine::GameFlags::kPaused) ||
			(gpGame->mGameFlags & engine::GameFlags::kSaveReplay) ||
			(gpGame->mGameFlags & engine::GameFlags::kLoadReplay))
		{
			throw std::runtime_error("navQueryActivation requires an unpaused normal server state");
		}
		else if (gpGame->mTimeStep.miTimeMultiply != 1 || gpGame->mTimeStep.miTimeDivide != 1)
		{
			throw std::runtime_error("navQueryActivation requires timescale 1/1");
		}
		else if (gpGame->mGameSaveLoad.IsRecording() || gpGame->mGameSaveLoad.IsReplaying())
		{
			throw std::runtime_error("navQueryActivation requires recording and replay to be inactive");
		}
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

	int64_t iQueuedAtTick = 0;
	int64_t iMinimumSampleTick = 0;
	if (bArmNavQuery)
	{
		if constexpr (kbProfiling)
		{
			// Prepare a complete queue copy before touching the profiler state. A failed allocation leaves both the
			// existing queue and the event arm unchanged.
			std::unordered_map<engine::GridCoord, std::vector<StatusChange>> preparedPendingAgentStatusChanges = gpServerSession->mpBroadcaster->mPendingAgentStatusChanges;
			for (const auto& [rCoord, rChange] : built)
			{
				preparedPendingAgentStatusChanges.try_emplace(rCoord).first->second.push_back(rChange);
			}

			// Prepare every response field that could allocate before arming. The command failure path must not report an
			// error after committing the event and queue transaction.
			rResult["injected"] = std::ssize(built);
			rResult["globalIds"] = std::move(globalIds);
			rResult["deferred"] = static_cast<bool>(gpGame->mGameFlags & engine::GameFlags::kPaused);

			// Drain is the main-thread serialization point. Keep the event arm and queue commit in one critical section
			// so another tick cannot move the floor or observe a partially committed transaction.
			std::lock_guard lock(gpProfileManager->mCpuTimerMutex);
			iQueuedAtTick = gpGame->TickCounter();
			constexpr int64_t kiMinimumSampleTickOffset = engine::kiTickRate + 1;
			if (iQueuedAtTick > std::numeric_limits<int64_t>::max() - kiMinimumSampleTickOffset)
			{
				throw std::runtime_error("navQueryActivation tick range exhausted");
			}
			iMinimumSampleTick = iQueuedAtTick + kiMinimumSampleTickOffset;
			rResult["navQueryActivation"] = {
				{"armed", true},
				{"queuedAtTick", iQueuedAtTick},
				{"minimumSampleTick", iMinimumSampleTick},
			};
			if (!gpProfileManager->ArmRawCpuTimerEventLocked(game::kCpuTimerPostRenderUpdateNavQuery, iMinimumSampleTick))
			{
				throw std::runtime_error("navQueryActivation event is occupied or overrun");
			}
			static_assert(noexcept(preparedPendingAgentStatusChanges.swap(gpServerSession->mpBroadcaster->mPendingAgentStatusChanges)));
			preparedPendingAgentStatusChanges.swap(gpServerSession->mpBroadcaster->mPendingAgentStatusChanges);
		}
	}
	else
	{
		for (const auto& [rCoord, rChange] : built)
		{
			gpServerSession->mpBroadcaster->QueueAgentStatusChange(rCoord, rChange);
		}
		rResult["injected"] = std::ssize(built);
		rResult["globalIds"] = std::move(globalIds);
		rResult["deferred"] = static_cast<bool>(gpGame->mGameFlags & engine::GameFlags::kPaused);
	}
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
	if (cmd == "replay_transfer_capture")
	{
		CommandReplayTransferCapture(rParams, rResult);
		return true;
	}
	if (cmd == "replay_drop_retained_end_frame")
	{
		CommandReplayDropRetainedEndFrame(rParams, rResult);
		return true;
	}
	if (cmd == "replay_inject_persistence_failure")
	{
		CommandReplayInjectPersistenceFailure(rParams, rResult);
		return true;
	}
	if (cmd == "replay_transfer_fixture")
	{
		CommandReplayTransferFixture(rParams, rResult);
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
	if (cmd == "query_profile")
	{
		CommandQueryProfile(rParams, rResult);
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
