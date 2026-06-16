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

	gpClientSession->mpClientNetwork->SendDesyncReport(rDesyncInfo.iDesyncTick, rDesyncInfo.desyncCoord, rDesyncInfo.desyncExpectedCrc, rDesyncInfo.desyncActualCrc);
	gpClientSession->mpClientNetwork->SendDebugFrameRequest(rDesyncInfo.iDesyncTick, rDesyncInfo.desyncCoord);
	gpClientSession->mpClientNetwork->SetDesyncDebugMode(true);

	mDesyncDebugState.iTick = rDesyncInfo.iDesyncTick;
	mDesyncDebugState.coord = rDesyncInfo.desyncCoord;
	mDesyncDebugState.pClientFrame = std::move(rDesyncInfo.pDesyncClientFrame);
	mDesyncDebugState.entryTime = std::chrono::steady_clock::now();
}

void ClientDesyncManager::PollDebugFrameResponse()
{
	std::unique_ptr<engine::ReceivedDebugFrame> pDebugFrame = gpClientSession->mpClientNetwork->DrainReceivedDebugFrame();
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
			ASSERT(false);
			std::snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Desynced from server");
			gpClientSession->mpClientNetwork->Disconnect();
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
		ASSERT(false);
		std::snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Desynced from server (debug frame timeout)");
		gpClientSession->mpClientNetwork->Disconnect();
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
		gpClientSession->mpClientNetwork->Disconnect();
		return;
	}

	gpClientSession->mpClientNetwork->SendResyncRequest();
	gpClientSession->mpClientNetwork->SetDesyncDebugMode(false);
	ResetCoordStatesForResync();
}

void ClientDesyncManager::ResetCoordStatesForResync()
{
	for (auto& [rCoord, rSub] : gpGame->mCoordFrames)
	{
		rSub.ResetClientState();
	}

	gpClientSession->mpReconciler->Reset();
	gpClientSession->ClearStickySubscriptions();
}

void ClientDesyncManager::Reset()
{
	mDesyncDebugState = {};
	miDesyncCount = 0;
}

#endif // BT_CLIENT

} // namespace game
