#include "Game.h"

#include "Network/Client/ClientReconciler.h"
#include "Network/Client/ReconcileReplay.h"
#include "Frame/Collections/Players/Players.h"
#include "Profile/ProfileManager.h"

namespace game
{

#if defined(BT_CLIENT)

static bool GetClientSnapshotPosition(XMVECTOR& rOut)
{
	auto coordIt = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
	if (coordIt == gpGame->mCoordFrames.end() || coordIt->second.iSnapshotCount <= 0)
	{
		return false;
	}
	int64_t iPhysical = engine::SnapshotIndex(coordIt->second.iSnapshotHead, coordIt->second.iSnapshotCount - 1);
	const std::unique_ptr<game::Frame>& pSnapshot = coordIt->second.snapshots[iPhysical];
	if (pSnapshot == nullptr)
	{
		return false;
	}
	std::optional<int64_t> oClientPlayerIndex = gpGame->ClientPlayerIndex(*pSnapshot->postRender.pPlayers);
	if (!oClientPlayerIndex.has_value())
	{
		return false;
	}
	rOut = pSnapshot->interpolate.pPlayers->pVecPositions[*oClientPlayerIndex];
	return true;
}

ReconcileDesyncInfo ClientReconciler::Run()
{
	// Heap: Frame allocation during replay, map operations on serverUpdates, and scratch resize
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Re-sync human identity from main thread
	mConfirmedClientState.clientGridCoord = gpGame->mClientGridCoord;
	mConfirmedClientState.clientGlobalPlayerId = gpGame->ClientPlayerId();
	mConfirmedClientState.fPreviousClientArmor = gpGame->PreviousClientArmor();

	ReconcileInputs inputs;
	inputs.confirmedClientState = mConfirmedClientState;
	inputs.uiNextFrameId = gpGame->NextFrameId();
	inputs.iTargetTick = gpGame->TickCounter();
	ASSERT(inputs.iTargetTick >= 0);
	common::LogTickScope logTickScope(inputs.iTargetTick);
	inputs.playerAlignment = gpGame->PlayerAlignment();
	inputs.alignments = gpGame->Alignments();
	inputs.iJitterUs = (gpClientSession->mpClientNetwork != nullptr) ? gpClientSession->mpClientNetwork->GetJitterUs() : 0;

	// Populate per-coord works (only eligible coords — those with iConfirmedTick >= 0).
	// Uses resize() + in-place assignment to retain CoordScratch::replayStack capacity
	// across Run() calls, avoiding per-frame heap churn.
	size_t eligibleCount = 0;
	for (const auto& [rCoord, rFrames] : gpGame->mCoordFrames)
	{
		if (rFrames.iConfirmedTick >= 0)
		{
			++eligibleCount;
		}
	}
	if (mWorks.size() < eligibleCount)
	{
		mWorks.resize(eligibleCount);
	}
	size_t iSlot = 0;
	for (auto& [rCoord, rFrames] : gpGame->mCoordFrames)
	{
		if (rFrames.iConfirmedTick < 0)
		{
			continue;
		}
		CoordWork& rWork = mWorks[iSlot++];
		rWork.coord = rCoord;
		rWork.pFrames = &rFrames;
		// Reset scratch fields but preserve replayStack capacity
		CoordScratch& rScratch = rWork.scratch;
		rScratch.replayStack.clear();
		rScratch.iReplayStackCount = 0;
		rScratch.iReplayWriteHead = 0;
		rScratch.iReplayWriteCount = 0;
		rScratch.iLastValidatedIndex = -1;
		rScratch.iNewConfirmedTick = -1;
		rScratch.iNewConfirmedOffset = -1;
		rScratch.iOutputCount = 0;
		rScratch.bCrcFastPath = false;
		rScratch.bReplayed = false;
		rScratch.bShrunkRollback = false;
		rScratch.bReSimOccurred = false;
		rScratch.bSuppressRepeatLogs = false;
		rScratch.iPreReconcileTailTick = -1;
		rScratch.iTickCounter = 0;
		rScratch.fCurrentTime = 0.0f;
		rScratch.profiling = {};
		rScratch.iDesyncTick = -1;
		rScratch.desyncExpectedCrc = 0;
		rScratch.desyncActualCrc = 0;
		rScratch.pDesyncClientFrame.reset();
	}
	const size_t iActiveCount = iSlot;

	if (iActiveCount == 0)
	{
		return {};
	}

	// Capture human pre-writeback position for visual error smoothing
	XMVECTOR vecPreWritebackPosition {};
	bool bCapturedPrePosition = GetClientSnapshotPosition(vecPreWritebackPosition);

	// Parallel per-coord reconciliation via the shared multithreading pool.
	// Each worker touches only its own CoordFrames entry — no cross-coord writes.
	std::span<CoordWork> activeWorks(mWorks.data(), iActiveCount);
	const int64_t iCount = static_cast<int64_t>(iActiveCount);
	auto processRange = [&](int64_t iBegin, int64_t iEnd)
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		for (int64_t i = iBegin; i < iEnd; ++i)
		{
			ReconcileCoord(activeWorks[i], inputs);
		}
	};
	common::gpMultithreading->Dispatch(iCount, processRange);

	// Post-dispatch merge: profiling, desync (first-wins), bAnyFullReplay
	ReconcileProfiling mergedProfiling;
	bool bAnyFullReplay = false;
	CoordWork* pDesyncWork = nullptr;
	for (CoordWork& rWork : activeWorks)
	{
		const CoordScratch& rScratch = rWork.scratch;
		mergedProfiling.iCrcValidatedFrameTicks += rScratch.profiling.iCrcValidatedFrameTicks;
		mergedProfiling.iAssumedFrameTicks += rScratch.profiling.iAssumedFrameTicks;
		mergedProfiling.iCrcFastPathEvents += rScratch.profiling.iCrcFastPathEvents;
		mergedProfiling.iStatusChangeReplayTicks += rScratch.profiling.iStatusChangeReplayTicks;
		mergedProfiling.iKnockOnReplayTicks += rScratch.profiling.iKnockOnReplayTicks;

		if (rScratch.iDesyncTick >= 0 && pDesyncWork == nullptr)
		{
			pDesyncWork = &rWork;
		}

		// Audio voice invalidation skip is gated on the human coord experiencing a full replay.
		if (rScratch.bReplayed && rWork.coord == mConfirmedClientState.clientGridCoord)
		{
			bAnyFullReplay = true;
		}

		if (rScratch.iDesyncTick >= 0 || (rScratch.bReplayed && rScratch.bReSimOccurred))
		{
			bool bLogThis = !rScratch.bSuppressRepeatLogs || (rWork.pFrames->iStuckFrameCount % engine::CoordFrames::kiStuckLogInterval == 0);
			if (bLogThis)
			{
				LOG(kNetwork, kVerbose, "Reconcile post-replay Coord: ({},{}) NewConfirmedTick: {} ReplayStackCount: {} LastValidatedIndex: {} CrcFastPath: {} Replayed: {} ShrunkRollback: {} DesyncTick: {}", rWork.coord.x, rWork.coord.y, rScratch.iNewConfirmedTick, rScratch.iReplayStackCount, rScratch.iLastValidatedIndex, rScratch.bCrcFastPath, rScratch.bReplayed, rScratch.bShrunkRollback, rScratch.iDesyncTick);
			}
		}
	}

	if (pDesyncWork != nullptr)
	{
		char acExpected[20] {}, acActual[20] {};
		common::ToHex(std::span<char, 20>(acExpected), pDesyncWork->scratch.desyncExpectedCrc);
		common::ToHex(std::span<char, 20>(acActual), pDesyncWork->scratch.desyncActualCrc);
		LOG(kNetwork, kVerbose, "Reconcile Desync Summary Coord: ({},{}) DesyncTick: {} ExpectedCrc: {} ActualCrc: {} ReplayTicks: {} NewConfirmed: {}", pDesyncWork->coord.x, pDesyncWork->coord.y, pDesyncWork->scratch.iDesyncTick, acExpected, acActual, pDesyncWork->scratch.iReplayStackCount, pDesyncWork->scratch.iNewConfirmedTick);

		ReconcileDesyncInfo desyncInfo;
		desyncInfo.bDesync = true;
		desyncInfo.iDesyncTick = pDesyncWork->scratch.iDesyncTick;
		desyncInfo.desyncCoord = pDesyncWork->coord;
		desyncInfo.desyncExpectedCrc = pDesyncWork->scratch.desyncExpectedCrc;
		desyncInfo.desyncActualCrc = pDesyncWork->scratch.desyncActualCrc;
		desyncInfo.pDesyncClientFrame = std::move(pDesyncWork->scratch.pDesyncClientFrame);
		return desyncInfo;
	}

	// Compute new confirmed client state (human coord time advance + transfer migration)
	ConfirmedClientState newConfirmedClientState = mConfirmedClientState;
	ReconcileUpdateClientState(activeWorks, inputs, bAnyFullReplay, newConfirmedClientState);

	// Visual error offset: pre/post human position delta accumulated into gpGame
	if (bCapturedPrePosition && bAnyFullReplay)
	{
		XMVECTOR vecPostWritebackPosition {};
		if (GetClientSnapshotPosition(vecPostWritebackPosition))
		{
			XMVECTOR vecError = XMVectorSubtract(vecPreWritebackPosition, vecPostWritebackPosition);
			XMVECTOR vecTotal = XMVectorAdd(gpGame->mVecVisualErrorOffset, vecError);
			float fTotal = XMVectorGetX(XMVector3Length(vecTotal));
			if (fTotal > Game::kfVisualErrorMaxDistance)
			{
				gpGame->mVecVisualErrorOffset = {};
				LOG(kNetwork, kWarning, "Visual error offset reset (exceeded max) Coord: ({},{}) Delta: {:.3f} Max: {:.1f}", gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y, XMVectorGetX(XMVector3Length(vecError)), Game::kfVisualErrorMaxDistance);
			}
			else
			{
				gpGame->mVecVisualErrorOffset = vecTotal;
				float fDelta = XMVectorGetX(XMVector3Length(vecError));
				if (fTotal > 0.1f)
				{
					float fChange = (mfLastLoggedVisualErrorDelta > 0.0f)
						? std::abs(fDelta - mfLastLoggedVisualErrorDelta) / mfLastLoggedVisualErrorDelta
						: 1.0f;
					if (fChange > 0.15f)
					{
						LOG(kNetwork, kVerbose, "Visual error offset Coord: ({},{}) Delta: {:.3f} Accumulated: {:.3f}", gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y, fDelta, fTotal);
						mfLastLoggedVisualErrorDelta = fDelta;
					}
				}
			}
		}
	}

	mConfirmedClientState = newConfirmedClientState;
	gpGame->SetPreviousClientArmor(newConfirmedClientState.fPreviousClientArmor);

	gpProfileManager->SetReconcileCounters(mergedProfiling.iCrcValidatedFrameTicks, mergedProfiling.iAssumedFrameTicks, mergedProfiling.iCrcFastPathEvents, mergedProfiling.iStatusChangeReplayTicks, mergedProfiling.iKnockOnReplayTicks);

	gpGame->SetNextFrameId(std::max(gpGame->NextFrameId(), inputs.uiNextFrameId));

	if (bAnyFullReplay && engine::gpAudioManager != nullptr)
	{
		engine::gpAudioManager->SkipNextStaticVoiceInvalidation();
	}

	return {};
}

void ClientReconciler::Reset()
{
	mConfirmedClientState = {};
	mWorks.clear();
	muiNextGeneration = 1;
	mfLastLoggedVisualErrorDelta = 0.0f;
}

#endif // BT_CLIENT

} // namespace game
