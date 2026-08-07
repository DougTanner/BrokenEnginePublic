#include "Network/Client/ClientSession.h"

#include "Fleet.h"
#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/GamePacketType.h"
#include "Network/PlayerEvents.h"
#include "Profile/ProfileManager.h"

namespace game
{

#if defined(BT_CLIENT)

const char* ToString(SubscriptionChangeReason eReason)
{
	switch (eReason)
	{
		case SubscriptionChangeReason::kAssigned:       return "kAssigned";
		case SubscriptionChangeReason::kSpawned:        return "kSpawned";
		case SubscriptionChangeReason::kChangedFrame:   return "kChangedFrame";
		case SubscriptionChangeReason::kDied:           return "kDied";
		case SubscriptionChangeReason::kFleetSync:      return "kFleetSync";
		case SubscriptionChangeReason::kPollTick:       return "kPollTick";
		case SubscriptionChangeReason::kFocusNextFleet: return "kFocusNextFleet";
		case SubscriptionChangeReason::kFocusPrevFleet: return "kFocusPrevFleet";
		case SubscriptionChangeReason::kSelectPlayer:   return "kSelectPlayer";
	}
	return "Unknown";
}

ClientSession::ClientSession()
{
	ASSERT(gpClientSession == nullptr);

	gpClientSession = this;
	mpDesyncManager = std::make_unique<ClientDesyncManager>();
	mpReconciler = std::make_unique<ClientReconciler>();
	mpRuntime = std::make_unique<engine::ClientSessionRuntime>(*this);
}

ClientSession::~ClientSession()
{
	mpRuntime.reset();
	if (gpClientSession == this)
	{
		gpClientSession = nullptr;
	}
}

template <typename TLogFunction, typename... TArgs>
void ClientSession::SendGameRequest(GamePacketType ePacketType, const TLogFunction& rLogFunction, const TArgs&... rArgs)
{
	if (mpRuntime->mpClient == nullptr || !(mpRuntime->mpClient->mStateFlags & engine::Client::ClientStateFlags::kConnected) || mpRuntime->mpClient->mpServerPeer == nullptr)
	{
		return;
	}

	std::optional<common::LogTickScope> optionalTickScope;
	if (common::gpThreadLocal->miLogTickCounter < 0)
	{
		optionalTickScope.emplace(gpGame->TickCounter());
	}

	rLogFunction();
	engine::gpClient->SendSimplePacket(ePacketType, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, rArgs...);

	{
		// Send the user command now instead of waiting for the tick-cadence ack flush.
		// Heap: ENet may allocate while flushing outgoing commands
		ScopedSuppressAllocationTracking suppress;
		mpRuntime->FlushOutgoing();
	}
}

void ClientSession::ProcessReceivedGamePackets()
{

	// Parse and process player events from raw game packets
	try
	{
		common::ScopedWorkbufferArena playerEventsArena = common::gpThreadLocal->mWorkbuffer.Push();
		ParsePlayerEvents(mpRuntime->mpClient->mReceivedGamePackets, playerEventsArena);
		const ReceivedPlayerEvent* pPlayerEvents = playerEventsArena.Data<ReceivedPlayerEvent>();
		int64_t iPlayerEventCount = playerEventsArena.Count<ReceivedPlayerEvent>();
		for (int64_t i = 0; i < iPlayerEventCount; ++i)
		{
			ApplyPlayerEvent(pPlayerEvents[i]);
		}
	}
	catch (const std::exception& rException)
	{
		LOG(kNetwork, kWarning, "ClientSession::ProcessReceivedGamePackets failed processing player events: {}", rException.what());
	}

	// Apply server timespeed updates from remaining game packets
	try
	{
		for (const std::pair<uint8_t, std::vector<uint8_t>>& rPacket : mpRuntime->mpClient->mReceivedGamePackets)
		{
			if (static_cast<GamePacketType>(rPacket.first) != GamePacketType::kServerTimespeedUpdate)
			{
				continue;
			}
			// 8B multiply + 8B divide = 16 bytes (type byte already stripped)
			if (rPacket.second.size() < 16)
			{
				continue;
			}
			const uint8_t* pCursor = rPacket.second.data();
			int64_t iMultiply = engine::ReadInt64(pCursor);
			int64_t iDivide = engine::ReadInt64(pCursor);
			LOG(kNetwork, kDebug, "ClientSession::ServerTimespeedUpdate Multiply: {} Divide: {}", iMultiply, iDivide);
			gpGame->mTimeStep.SetTimeScale(iMultiply, iDivide);
		}
	}
	catch (const std::exception& rException)
	{
		LOG(kNetwork, kWarning, "ClientSession::ProcessReceivedGamePackets failed processing server timespeed updates: {}", rException.what());
	}

	// Parse fleet sync from remaining game packets
	try
	{
		std::vector<Fleet> receivedFleets;
		if (ParseFleetSync(mpRuntime->mpClient->mReceivedGamePackets, receivedFleets))
		{
			engine::GridCoord preFleetCoord = gpGame->mClientGridCoord;
			gpGame->SyncFleets(std::move(receivedFleets));
			if (gpGame->mClientGridCoord != preFleetCoord)
			{
				UpdateDesiredCoords(SubscriptionChangeReason::kFleetSync);
			}
		}
	}
	catch (const std::exception& rException)
	{
		LOG(kNetwork, kWarning, "ClientSession::ProcessReceivedGamePackets failed processing fleet sync: {}", rException.what());
	}

}

void ClientSession::ApplyPlayerEvent(const ReceivedPlayerEvent& rEvent)
{
	switch (rEvent.eType)
	{
		case PlayerEventType::kAssigned:
			if (!gpGame->IsClientPlayer(rEvent.globalPlayerId))
			{
				LOG(kNetwork, kVerbose, "PlayerEvent kAssigned NewGlobalPlayerId: {} NewCoord: ({},{}) FocusedGlobalPlayerId: {} FocusedCoord: ({},{})", rEvent.globalPlayerId, rEvent.coord.x, rEvent.coord.y, gpGame->ClientPlayerId(), gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y);
				gpGame->AddClientPlayer(rEvent.globalPlayerId, rEvent.coord);
			}
			UpdateDesiredCoords(SubscriptionChangeReason::kAssigned);
			break;
		case PlayerEventType::kSpawned:
			UpdatePlayerCoord(rEvent.globalPlayerId, rEvent.coord);
			if (rEvent.globalPlayerId == gpGame->ClientPlayerId())
			{
				gpGame->SetClientGridCoord(rEvent.coord);
			}
			UpdateDesiredCoords(SubscriptionChangeReason::kSpawned);
			break;
		case PlayerEventType::kChangedFrame:
			UpdatePlayerCoord(rEvent.globalPlayerId, rEvent.coord);
			if (rEvent.globalPlayerId == gpGame->ClientPlayerId())
			{
				gpGame->SetClientGridCoord(rEvent.coord);
			}
			UpdateDesiredCoords(SubscriptionChangeReason::kChangedFrame);
			break;
		case PlayerEventType::kDied:
			gpGame->RemoveClientPlayer(rEvent.globalPlayerId);
			UpdateDesiredCoords(SubscriptionChangeReason::kDied);
			break;
	}
}

void ClientSession::UpdatePlayerCoord(engine::global_id_t globalPlayerId, engine::GridCoord coord)
{
	for (int64_t i = 0; i < gpGame->PlayerCount(); ++i)
	{
		if (gpGame->mClientPlayerIds.at(i) == globalPlayerId)
		{
			gpGame->mClientPlayerCoords.at(i) = coord;
			break;
		}
	}
}

void ClientSession::Poll()
{
	ASSERT(common::gpMultithreading->IsMainThread());
	// Heap: transport receive buffers and game packet/frame adoption
	ScopedSuppressAllocationTracking suppress;
	engine::NetworkTimeState networkTimeState {
		.bFastForward = gpGame->mTimeStep.miTimeMultiply > 1,
		.iExpectedUpdateIntervalMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(gpGame->mTimeStep.SimToWall(NetworkSessionContract::kTickDuration)).count(),
		.iExpectedUpdatesPerSecond = engine::kiTickRate * gpGame->mTimeStep.miTimeMultiply / gpGame->mTimeStep.miTimeDivide,
	};
	mpRuntime->PollAndDrain(networkTimeState);
}

void ClientSession::Reconcile()
{
	if (mpDesyncManager->IsStalled())
	{
		return;
	}

	gpProfileManager->CpuStart(engine::kCpuTimerNetworkPollReconcile);
	{
		// Heap: reconciliation deserialization and map operations
		ScopedSuppressAllocationTracking suppress;
		int64_t iCurrentTick = gpGame->TickCounter();
		if (engine::gpClient != nullptr)
		{
			ReconcileDesyncInfo desyncInfo = mpReconciler->Run();
			if (desyncInfo.bDesync)
			{
				mpDesyncManager->OnDesyncDetected(std::move(desyncInfo));
			}
		}
		std::chrono::nanoseconds clockCorrectionNs = mpRuntime->EvaluateClock(iCurrentTick);
		gpProfileManager->SetClockCorrection(mpRuntime->miClockOffset, mpRuntime->miClockTargetBehind, mpRuntime->miClockError);

		if (mpRuntime->miLatestServerTick >= 0 && std::abs(mpRuntime->miClockError) >= engine::kiClockSnapThreshold)
		{
			// Snap tick counter to recover from extreme clock error. Sim runs BEHIND latestServerTick.
			// Clamp at 0 so a fresh post-load server (latestServerTick < currentTargetBehind)
			// doesn't drive the client tick negative.
			int64_t iSnapTick = std::max<int64_t>(0, mpRuntime->miLatestServerTick - mpRuntime->miCurrentTargetBehind);
			LOG(kNetwork, kWarning, "ClientSession::Reconcile Clock snap OldTick: {} NewTick: {} LatestServerTick: {} TargetBehind: {}", iCurrentTick, iSnapTick, mpRuntime->miLatestServerTick, mpRuntime->miCurrentTargetBehind);
			gpGame->SetTickCounter(iSnapTick);
			gpGame->mTimeStep.ClearAccumulator();
			gpGame->ResetRenderClock();
			mpRuntime->miClockError = 0;
		}
		else
		{
			gpGame->mTimeStep.mTickRemainderNs += clockCorrectionNs;
		}
	}
	gpProfileManager->CpuStop(engine::kCpuTimerNetworkPollReconcile, engine::CpuStopFlags::kSmoothNow);
}

void ClientSession::ConnectToServer(std::string_view serverAddress)
{
	gpGame->mModalMessage[0] = '\0';
	mpRuntime->Connect(serverAddress, engine::kuiDefaultPort, NetworkSessionContract::kiCoordSlots);
}

void ClientSession::OnConnectionRejected(const char* pcReason)
{
	std::snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "%s", pcReason);
	gpGame->meUiState = UiState::kModal;
}

void ClientSession::OnConnectionFailed()
{
	std::snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Connection failed");
	gpGame->meUiState = UiState::kModal;
}

void ClientSession::OnConnectionAccepted()
{
	if (gpGame->InMainMenu())
	{
		gpGame->StartGameMusic();
		gpGame->CreateNewFrame(GameFlags::kGame);
		gpGame->mGameFlags.Clear(engine::GameFlags::kMainMenu);
		gpGame->Reset();
		gpGame->meUiState = UiState::kNone;
	}
}

void ClientSession::PollDesyncState()
{
	mpDesyncManager->PollDebugFrameResponse();
	mpDesyncManager->PollDesyncTimeout();
}

void ClientSession::OnConnectionLost()
{
	gpGame->ChangeFrame(GameFlags::kMainMenu);
	if (gpGame->mModalMessage[0] == '\0')
	{
		std::snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Connection lost");
	}
	gpGame->meUiState = UiState::kModal;
}

void ClientSession::OnServerLoad()
{
	LOG(kDefault, kDebug, "ClientSession::OnServerLoad");

	// Reset tick counter and time step — server tick resets to the saved value
	gpGame->SetTickCounter(0);
	gpGame->mTimeStep.ClearAccumulator();
	gpGame->mTimeStep.mRealTime.Reset();
	gpGame->ResetRenderClock();

	// Clear player identity — server will reassign
	gpGame->mClientPlayerIds.clear();
	gpGame->mClientPlayerCoords.clear();
	gpGame->SetClientGridCoord({});
	gpGame->SetPreviousClientArmor(0.0f);
	gpGame->mVecVisualErrorOffset = {};
	gpGame->mWeaponModeToggle.Reset();
	gpGame->mNavigationDelayControl.Reset();

	// Clear fleet state — server will re-sync
	gpGame->mFleetSelection.Clear();

	// Clear local coord frames (stale pre-load data). Reset render-progress fields first
	// so that any entry re-emplaced by a racing packet in the same frame starts clean.
	for (auto& [rCoord, rCoordFrames] : gpGame->mCoordFrames)
	{
		rCoordFrames.ResetClientState();
	}
	gpGame->mCoordFrames.clear();

	// Reset game-owned reconciliation and desync state.
	mpReconciler->Reset();
	mpDesyncManager->Reset();
}

void ClientSession::OnRuntimeDisconnected()
{
	mpReconciler->Reset();
	for (auto& [rCoord, rFrames] : gpGame->mCoordFrames)
	{
		rFrames.ResetClientState();
	}
	mpDesyncManager->Reset();
}

void ClientSession::OnCoordReleased(engine::GridCoord coord)
{
	gpGame->mCoordFrames.erase(coord);
}
void ClientSession::SendUpdatePlayerRequest(int64_t iGlobalPlayerId, bool bUseMissiles, float fNavigationDelay)
{
	SendGameRequest(GamePacketType::kClientUpdatePlayerRequest, [&]
	{
		LOG(kNetwork, kVerbose, "ClientSession::SendUpdatePlayerRequest GlobalPlayer: {} Missiles: {} NavDelay: {}", iGlobalPlayerId, bUseMissiles, common::Wb(fNavigationDelay, 3));
	}, iGlobalPlayerId, static_cast<uint8_t>(bUseMissiles ? 1 : 0), fNavigationDelay);
}

void ClientSession::SendCreateFleetRequest()
{
	SendGameRequest(GamePacketType::kClientCreateFleetRequest, []
	{
		LOG(kNetwork, kDebug, "ClientSession::SendCreateFleetRequest");
	});
}

void ClientSession::SendDeleteFleetRequest(const FleetGuid& rFleetGuid)
{
	SendGameRequest(GamePacketType::kClientDeleteFleetRequest, [&]
	{
		LOG(kNetwork, kDebug, "ClientSession::SendDeleteFleetRequest Fleet: ({},{})", rFleetGuid.uiHigh, rFleetGuid.uiLow);
	}, rFleetGuid.uiHigh, rFleetGuid.uiLow);
}

void ClientSession::SendSpawnIntoFleetRequest(const FleetGuid& rFleetGuid)
{
	SendGameRequest(GamePacketType::kClientSpawnIntoFleetRequest, [&]
	{
		LOG(kNetwork, kDebug, "ClientSession::SendSpawnIntoFleetRequest Fleet: ({},{})", rFleetGuid.uiHigh, rFleetGuid.uiLow);
	}, rFleetGuid.uiHigh, rFleetGuid.uiLow);
}

void ClientSession::SendRespawnInFleetRequest(const FleetGuid& rFleetGuid, int64_t iMemberIndex)
{
	SendGameRequest(GamePacketType::kClientRespawnInFleetRequest, [&]
	{
		LOG(kNetwork, kDebug, "ClientSession::SendRespawnInFleetRequest Fleet: ({},{}) Member: {}", rFleetGuid.uiHigh, rFleetGuid.uiLow, iMemberIndex);
	}, rFleetGuid.uiHigh, rFleetGuid.uiLow, iMemberIndex);
}

void ClientSession::SendFleetNavigationDelayRequest(const FleetGuid& rFleetGuid, float fDelay)
{
	SendGameRequest(GamePacketType::kClientFleetNavigationDelay, [&]
	{
		LOG(kNetwork, kDebug, "ClientSession::SendFleetNavigationDelayRequest Fleet: ({},{}) Delay: {}", rFleetGuid.uiHigh, rFleetGuid.uiLow, common::Wb(fDelay, 3));
	}, rFleetGuid.uiHigh, rFleetGuid.uiLow, fDelay);
}

#endif // BT_CLIENT

} // namespace game
