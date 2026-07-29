#include "Network/Client/ClientDesyncManager.h"

#include "Game.h"
#include "Network/Client/ClientSession.h"

namespace game
{

#if defined(BT_CLIENT)

void ClientDesyncManager::OnDesyncDetected(ReconcileDesyncInfo&& rDesyncInfo)
{
	// Heap: Network sends for desync reporting
	ScopedSuppressAllocationTracking suppress;

	gpClientSession->mpRuntime->mpClient->SendDesyncReport(rDesyncInfo.iDesyncTick, rDesyncInfo.desyncCoord, rDesyncInfo.desyncExpectedCrc, rDesyncInfo.desyncActualCrc);
	if constexpr (kbDesyncDebugFrames)
	{
		gpClientSession->mpRuntime->mpClient->SendDebugFrameRequest(rDesyncInfo.iDesyncTick, rDesyncInfo.desyncCoord);
		gpClientSession->mpRuntime->mpClient->mStateFlags.Set(engine::Client::ClientStateFlags::kDesyncDebugMode);

		mDesyncDebugState.iTick = rDesyncInfo.iDesyncTick;
		mDesyncDebugState.coord = rDesyncInfo.desyncCoord;
		mDesyncDebugState.pClientFrame = std::move(rDesyncInfo.pDesyncClientFrame);
		mDesyncDebugState.entryTime = std::chrono::steady_clock::now();
	}
	else if constexpr (kbDesyncRecovery)
	{
		RecoverFromDesync();
	}
	else
	{
		std::snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Desynced from server");
		gpClientSession->mpRuntime->mpClient->Disconnect();
	}
}

void ClientDesyncManager::PollDebugFrameResponse()
{
	std::unique_ptr<engine::ReceivedDebugFrame> pDebugFrame = std::move(gpClientSession->mpRuntime->mpClient->mpReceivedDebugFrame);
	if (pDebugFrame != nullptr && mDesyncDebugState.pClientFrame != nullptr)
	{
		LOG(kNetwork, kError, "ClientDesyncManager::PollDebugFrameResponse Frame: {} Coord: ({},{}) matched, dumping diff", mDesyncDebugState.iTick, mDesyncDebugState.coord.x, mDesyncDebugState.coord.y);
		mDesyncDebugState.pClientFrame->LogDifferences(*pDebugFrame->pFrame);
		mDesyncDebugState = {};

		if constexpr (kbDesyncRecovery)
		{
			if constexpr (kbDebugBreak)
			{
				DEBUG_BREAK();
			}

			RecoverFromDesync();
		}
		else
		{
			std::snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Desynced from server");
			gpClientSession->mpRuntime->mpClient->Disconnect();
		}
	}
}

bool ClientDesyncManager::PollDesyncTimeout()
{
	if (mDesyncDebugState.iTick < 0 || std::chrono::steady_clock::now() - mDesyncDebugState.entryTime <= kDesyncDebugTimeout)
	{
		return false;
	}

	mDesyncDebugState = {};
	if constexpr (kbDesyncRecovery)
	{
		LOG(kNetwork, kError, "ClientDesyncManager::PollDesyncTimeout Desync debug mode timed out, recovering without debug frame");
		RecoverFromDesync();
	}
	else
	{
		LOG(kNetwork, kError, "ClientDesyncManager::PollDesyncTimeout Desync debug mode timed out, disconnecting");
		std::snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Desynced from server (debug frame timeout)");
		gpClientSession->mpRuntime->mpClient->Disconnect();
	}
	return true;
}

void ClientDesyncManager::RecoverFromDesync()
{
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	if (miDesyncCount > 0 && now - mFirstDesyncTime > kDesyncWindowDuration)
	{
		miDesyncCount = 0;
	}

	if (miDesyncCount == 0)
	{
		mFirstDesyncTime = now;
	}
	++miDesyncCount;

	LOG(kNetwork, kError, "ClientDesyncManager::RecoverFromDesync DesyncCount: {} / {}", miDesyncCount, kiMaxDesyncsBeforeDisconnect);

	if (miDesyncCount >= kiMaxDesyncsBeforeDisconnect)
	{
		LOG(kNetwork, kError, "ClientDesyncManager::RecoverFromDesync Escalating to disconnect");
		std::snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Desynced from server");
		gpClientSession->mpRuntime->mpClient->Disconnect();
		return;
	}

	gpClientSession->mpRuntime->mpClient->SendResyncRequest();
	gpClientSession->mpRuntime->mpClient->mStateFlags.Clear(engine::Client::ClientStateFlags::kDesyncDebugMode);
	ResetCoordStatesForResync();
}

void ClientDesyncManager::ResetCoordStatesForResync()
{
	for (auto& [rCoord, rSub] : gpGame->mCoordFrames)
	{
		rSub.ResetClientState();
	}

	gpClientSession->mpReconciler->Reset();
	gpClientSession->mpRuntime->mUnwantedTimestamps.clear();
}

void ClientDesyncManager::Reset()
{
	if (engine::gpClient != nullptr)
	{
		gpClientSession->mpRuntime->mpClient->mStateFlags.Clear(engine::Client::ClientStateFlags::kDesyncDebugMode);
	}
	mDesyncDebugState = {};
	mAgentFullStateFixtureState = {};
	miDesyncCount = 0;
}

void ClientDesyncManager::ArmAgentFullStateFixture(int64_t iTick, engine::GridCoord coord)
{
	ASSERT(!IsStalled());
	mAgentFullStateFixtureState.bStalled = true;
	mAgentFullStateFixtureState.iTick = iTick;
	mAgentFullStateFixtureState.coord = coord;
	gpClientSession->mpRuntime->mpClient->mStateFlags.Set(engine::Client::ClientStateFlags::kDesyncDebugMode);
	gpClientSession->mpRuntime->mpClient->SendResyncRequest();
}

void ClientDesyncManager::ClearAgentFullStateFixture()
{
	mAgentFullStateFixtureState = {};
	if (engine::gpClient != nullptr && mDesyncDebugState.iTick < 0)
	{
		gpClientSession->mpRuntime->mpClient->mStateFlags.Clear(engine::Client::ClientStateFlags::kDesyncDebugMode);
	}
}

#endif // BT_CLIENT

} // namespace game
