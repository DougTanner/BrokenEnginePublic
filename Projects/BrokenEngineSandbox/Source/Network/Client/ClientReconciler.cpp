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
	ScopedSuppressAllocationTracking suppress;

	// Re-sync client identity from main thread
	mConfirmedClientState.clientGridCoord = gpGame->mClientGridCoord;
	mConfirmedClientState.clientGlobalPlayerId = gpGame->ClientPlayerId();
	mConfirmedClientState.fPreviousClientArmor = gpGame->PreviousClientArmor();

	ReconcileInputs inputs;
	inputs.iTargetTick = gpGame->TickCounter();
	ASSERT(inputs.iTargetTick >= 0);
	common::LogTickScope logTickScope(inputs.iTargetTick);

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
		rWork.scratch.Reset();
	}
	const size_t iActiveCount = iSlot;

	if (iActiveCount == 0)
	{
		return {};
	}

	// Capture client pre-writeback position for visual error smoothing
	XMVECTOR vecPreWritebackPosition {};
	bool bCapturedPrePosition = GetClientSnapshotPosition(vecPreWritebackPosition);

	// Parallel per-coord reconciliation via the shared multithreading pool.
	// Each worker touches only its own CoordFrames entry — no cross-coord writes.
	std::span<CoordWork> activeWorks(mWorks.data(), iActiveCount);
	const int64_t iCount = static_cast<int64_t>(iActiveCount);
	auto processRange = [&](int64_t iBegin, int64_t iEnd)
	{
		// Heap: ReconcileCoord may grow per-coord scratch (frames, replay buffers) on dispatch
		ScopedSuppressAllocationTracking suppress;
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

		using enum ReconcileScratchFlags;

		// Audio voice invalidation skip is gated on the client coord experiencing a full replay.
		if ((rScratch.flags & kReplayed) && rWork.coord == mConfirmedClientState.clientGridCoord)
		{
			bAnyFullReplay = true;
		}

		if (rScratch.iDesyncTick >= 0 || ((rScratch.flags & kReplayed) && (rScratch.flags & kReSimOccurred)))
		{
			bool bLogThis = !(rScratch.flags & kSuppressRepeatLogs) || (rWork.pFrames->iStuckFrameCount % engine::CoordFrames::kiStuckLogInterval == 0);
			if (bLogThis)
			{
				if (rScratch.iDesyncTick >= 0)
				{
					LOG(kNetwork, kDebug, "Reconcile post-replay Rollback/replay exhausted with unresolved CRC mismatch Coord: ({},{}) NewConfirmedTick: {} Validated: {}/{} CrcFastPath: {} Replayed: {} ShrunkRollback: {} DesyncTick: {}", rWork.coord.x, rWork.coord.y, rScratch.iNewConfirmedTick, rScratch.iLastValidatedIndex + 1, rScratch.iReplayStackCount, static_cast<bool>(rScratch.flags & kCrcFastPath), static_cast<bool>(rScratch.flags & kReplayed), static_cast<bool>(rScratch.flags & kShrunkRollback), rScratch.iDesyncTick);
				}
				else
				{
					LOG(kNetwork, kDebug, "Reconcile post-replay Reconciliation resimulation completed without an unresolved CRC mismatch Coord: ({},{}) NewConfirmedTick: {} Validated: {}/{} CrcFastPath: {} Replayed: {} ShrunkRollback: {} DesyncTick: {}", rWork.coord.x, rWork.coord.y, rScratch.iNewConfirmedTick, rScratch.iLastValidatedIndex + 1, rScratch.iReplayStackCount, static_cast<bool>(rScratch.flags & kCrcFastPath), static_cast<bool>(rScratch.flags & kReplayed), static_cast<bool>(rScratch.flags & kShrunkRollback), rScratch.iDesyncTick);
				}
			}
		}
	}

	if (pDesyncWork != nullptr)
	{
		char acExpected[20] {}, acActual[20] {};
		common::ToHex(std::span<char, 20>(acExpected), pDesyncWork->scratch.desyncExpectedCrc);
		common::ToHex(std::span<char, 20>(acActual), pDesyncWork->scratch.desyncActualCrc);
		LOG(kNetwork, kError, "CONFIRMED DESYNC after full rollback/replay Coord: ({},{}) DesyncTick: {} ExpectedCrc: {} ActualCrc: {} ReplayTicks: {} NewConfirmed: {}", pDesyncWork->coord.x, pDesyncWork->coord.y, pDesyncWork->scratch.iDesyncTick, acExpected, acActual, pDesyncWork->scratch.iReplayStackCount, pDesyncWork->scratch.iNewConfirmedTick);

		ReconcileDesyncInfo desyncInfo;
		desyncInfo.bDesync = true;
		desyncInfo.iDesyncTick = pDesyncWork->scratch.iDesyncTick;
		desyncInfo.desyncCoord = pDesyncWork->coord;
		desyncInfo.desyncExpectedCrc = pDesyncWork->scratch.desyncExpectedCrc;
		desyncInfo.desyncActualCrc = pDesyncWork->scratch.desyncActualCrc;
		desyncInfo.pDesyncClientFrame = std::move(pDesyncWork->scratch.pDesyncClientFrame);
		return desyncInfo;
	}

	// Compute new confirmed client state (client coord time advance + transfer migration)
	ConfirmedClientState newConfirmedClientState = mConfirmedClientState;
	ReconcileUpdateClientState(activeWorks, bAnyFullReplay, newConfirmedClientState);

	// Visual error offset: pre/post client position delta accumulated into gpGame
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
				LOG(kNetwork, kWarning, "Visual error offset reset (exceeded max) Coord: ({},{}) Delta: {} Max: {}", gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y, common::Wb(XMVectorGetX(XMVector3Length(vecError)), 3), common::Wb(Game::kfVisualErrorMaxDistance, 1));
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
					int64_t iCurrentTick = gpGame->TickCounter();
					if (fChange > 0.15f && (iCurrentTick - miLastVisualErrorLogTick > 32))
					{
						LOG(kNetwork, kDebug, "Visual error offset Coord: ({},{}) Delta: {} Accumulated: {}", gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y, common::Wb(fDelta, 3), common::Wb(fTotal, 3));
						mfLastLoggedVisualErrorDelta = fDelta;
						miLastVisualErrorLogTick = iCurrentTick;
					}
				}
			}
		}
	}

	mConfirmedClientState = newConfirmedClientState;
	gpGame->SetPreviousClientArmor(newConfirmedClientState.fPreviousClientArmor);

	gpProfileManager->SetReconcileCounters(mergedProfiling.iCrcValidatedFrameTicks, mergedProfiling.iAssumedFrameTicks, mergedProfiling.iCrcFastPathEvents, mergedProfiling.iStatusChangeReplayTicks, mergedProfiling.iKnockOnReplayTicks);

	// Large single-frame re-sim bursts are frame-time spike candidates
	int64_t iReSimTicks = mergedProfiling.iStatusChangeReplayTicks + mergedProfiling.iKnockOnReplayTicks;
	if (iReSimTicks >= 8)
	{
		LOG(kNetwork, kVerbose, "Replay burst ReSimTicks: {} StatusChange: {} KnockOn: {} Assumed: {} CrcValidated: {} Coords: {}", iReSimTicks, mergedProfiling.iStatusChangeReplayTicks, mergedProfiling.iKnockOnReplayTicks, mergedProfiling.iAssumedFrameTicks, mergedProfiling.iCrcValidatedFrameTicks, iActiveCount);
	}

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
	mfLastLoggedVisualErrorDelta = 0.0f;
	miLastVisualErrorLogTick = -1000;
}

#endif // BT_CLIENT

} // namespace game
