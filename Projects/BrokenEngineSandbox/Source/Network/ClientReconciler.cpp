#include "Game.h"

#include "Network/ClientReconciler.h"
#include "Network/ReconcileReplay.h"
#include "Frame/Collections/Players/Players.h"
#include "Profile/ProfileManager.h"

namespace game
{

#if defined(BT_CLIENT)

ClientReconciler::ClientReconciler()
{
	if constexpr (kbEnableReconcileThread)
	{
		mpWorker = std::make_unique<common::PersistentWorker>(common::kThreadReconcile, 10 * 1'024 * 1'024);
	}
}

ClientReconciler::~ClientReconciler()
{
	if constexpr (kbEnableReconcileThread)
	{
		if (mbInFlight)
		{
			mpWorker->Wait();
			mbInFlight = false;
		}
	}
}

void ClientReconciler::TryKick()
{
	if (!mbHasNewData || mbInFlight)
	{
		return;
	}

	Kick();
	mbInFlight = true;
	mbHasNewData = false;
}

ReconcileDesyncInfo ClientReconciler::Wait()
{
	if constexpr (kbEnableReconcileThread)
	{
		if (!mbInFlight)
		{
			return {};
		}

		mpWorker->Wait();
		ReconcileDesyncInfo desyncInfo = ApplyResult();
		mbInFlight = false;
		return desyncInfo;
	}
	else
	{
		return {};
	}
}

void ClientReconciler::Reset()
{
	if constexpr (kbEnableReconcileThread)
	{
		if (mbInFlight)
		{
			mpWorker->Wait();
			mbInFlight = false;
		}
		mpContext.reset();
	}

	mbHasNewData = false;
	mConfirmedHumanState = {};
}

void ClientReconciler::Kick()
{
	mpContext = std::make_unique<ReconcileContext>();
	ReconcileContext& rReconcileContext = *mpContext;

	// Populate per-coord work items
	rReconcileContext.coordWork.reserve(gpGame->mCoordFrames.size());
	for (auto& [rCoord, rSub] : gpGame->mCoordFrames)
	{
		if (rSub.iConfirmedTick < 0)
		{
			continue;
		}

		CoordReconcileWork work;
		work.coord = rCoord;
		work.uiGeneration = rSub.uiGeneration;
		work.iConfirmedTick = rSub.iConfirmedTick;
		work.iConfirmedOffset = rSub.iConfirmedOffset;
		work.iSnapshotHead = rSub.iSnapshotHead;
		work.serverUpdates = std::move(rSub.serverUpdates);
		work.pendingFullState = std::move(rSub.pendingFullState);

		// Swap snapshot array to worker
		std::swap(work.snapshots, rSub.snapshots);
		work.iSnapshotCount = rSub.iSnapshotCount;
		rSub.iSnapshotHead = 0;
		rSub.iSnapshotCount = 0;
		rSub.iConfirmedOffset = -1;
		rSub.serverUpdates.clear();
		rSub.pendingFullState.reset();

		rReconcileContext.coordWork.push_back(std::move(work));
	}

	// Re-sync human identity from main thread (updated by PollNetwork before Kick)
	mConfirmedHumanState.humanGridCoord = gpGame->mHumanGridCoord;
	mConfirmedHumanState.humanPlayerId = gpGame->HumanPlayerId();
	mConfirmedHumanState.fPreviousHumanArmor = gpGame->PreviousHumanArmor();

	// Global input
	rReconcileContext.confirmedHumanState = mConfirmedHumanState;
	rReconcileContext.uiNextFrameId = gpGame->NextFrameId();
	rReconcileContext.iTargetTick = gpGame->TickCounter();
	ASSERT(rReconcileContext.iTargetTick >= 0);
	rReconcileContext.playerAlignment = gpGame->PlayerAlignment();
	rReconcileContext.alignments = gpGame->Alignments();
	rReconcileContext.iJitterUs = (gpClientSession->mpClientNetwork != nullptr) ? gpClientSession->mpClientNetwork->GetJitterUs() : 0;

	// Create/resize per-coord dispatch pool
	if constexpr (kbEnableReconcileDispatch)
	{
		int64_t iDesiredWorkers = static_cast<int64_t>(rReconcileContext.coordWork.size()) - 1;
		if (iDesiredWorkers > 0 && (!mpDispatch || mpDispatch->WorkerCount() < iDesiredWorkers))
		{
			// Heap: Dispatch pool creation (rare, only when coord count grows)
			ScopedSuppressAllocationTracking suppressAllocationTracking;
			mpDispatch = std::make_unique<common::Multithreading>(common::kThreadReconcileDispatch, iDesiredWorkers, 10 * 1'024 * 1'024);
		}
	}

	// Dispatch to worker
	mpWorker->Wake([this]()
	{
		Reconcile(*mpContext);
	});
}

static void ReconcileMergeResults(ReconcileContext& rReconcileContext)
{
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		// Aggregate profiling
		rReconcileContext.profiling.iCrcValidatedFrameTicks += rWork.profiling.iCrcValidatedFrameTicks;
		rReconcileContext.profiling.iAssumedFrameTicks += rWork.profiling.iAssumedFrameTicks;
		rReconcileContext.profiling.iCrcFastPathEvents += rWork.profiling.iCrcFastPathEvents;
		rReconcileContext.profiling.iStatusChangeReplayTicks += rWork.profiling.iStatusChangeReplayTicks;
		rReconcileContext.profiling.iKnockOnReplayTicks += rWork.profiling.iKnockOnReplayTicks;

		// Check for desync (first one wins)
		if (rWork.iDesyncTick >= 0 && rReconcileContext.iDesyncTick < 0)
		{
			rReconcileContext.iDesyncTick = rWork.iDesyncTick;
			rReconcileContext.desyncCoord = rWork.coord;
			rReconcileContext.desyncExpectedCrc = rWork.desyncExpectedCrc;
			rReconcileContext.desyncActualCrc = rWork.desyncActualCrc;
			rReconcileContext.pDesyncClientFrame = std::move(rWork.pDesyncClientFrame);
		}

		// Track full replay (only human coord triggers bAnyFullReplay — it gates SetTickCounter/SetCurrentTime in ApplyResult)
		if (rWork.bFullReplay && rWork.coord == rReconcileContext.confirmedHumanState.humanGridCoord)
		{
			rReconcileContext.bAnyFullReplay = true;
			rReconcileContext.iTickCounter = rWork.iTickCounter;
		}
	}
}

void ClientReconciler::Reconcile(ReconcileContext& rReconcileContext)
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbEnableReconcileDispatch)
	{
		if (mpDispatch)
		{
			// Parallel per-coord reconciliation
			auto processRange = [&](int64_t iStart, int64_t iEnd)
			{
				ScopedSuppressAllocationTracking suppressAllocationTracking;
				for (int64_t i = iStart; i < iEnd; ++i)
				{
					ReconcileCoord(rReconcileContext, rReconcileContext.coordWork[i]);
				}
			};
			mpDispatch->Dispatch(static_cast<int64_t>(rReconcileContext.coordWork.size()), processRange);
		}
		else
		{
			for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
			{
				ReconcileCoord(rReconcileContext, rWork);
			}
		}
	}
	else
	{
		// Sequential (existing behavior)
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			ReconcileCoord(rReconcileContext, rWork);
			if (rWork.iDesyncTick >= 0)
			{
				break;
			}
		}
	}

	ReconcileMergeResults(rReconcileContext);
	if (rReconcileContext.iDesyncTick >= 0)
	{
		return;
	}

	ReconcileUpdateHumanState(rReconcileContext);
}

static void RestoreUnconsumedUpdates(CoordReconcileWork& rWork, engine::CoordFrames& rSub)
{
	for (auto& [iTick, rUpdate] : rWork.serverUpdates)
	{
		if (iTick > rSub.iConfirmedTick)
		{
			rSub.serverUpdates.insert_or_assign(iTick, std::move(rUpdate));
		}
	}

	if (rWork.pendingFullState.has_value() && (!rSub.pendingFullState.has_value() || rWork.pendingFullState->iTick >= rSub.pendingFullState->iTick))
	{
		rSub.pendingFullState = std::move(rWork.pendingFullState);
	}
}

void ClientReconciler::ApplyCoordWriteback(CoordReconcileWork& rWork, engine::CoordFrames& rSub)
{
	if (rWork.iNewConfirmedTick >= 0)
	{
		rSub.iConfirmedTick = rWork.iNewConfirmedTick;
	}

	int64_t iMainSnapshotCount = rSub.iSnapshotCount;
	std::swap(rSub.snapshots, rWork.snapshots);

	if (rWork.iNewConfirmedTick >= 0)
	{
		rSub.iSnapshotHead = rWork.iNewConfirmedOffset;
		rSub.iConfirmedOffset = 0;
		rSub.iSnapshotCount = rWork.iOutputCount;
		ASSERT(rSub.iSnapshotCount >= 0 && rSub.iSnapshotCount <= engine::kiNetworkBufferSize);
	}
	else
	{
		rSub.iSnapshotHead = rWork.iSnapshotHead;
		rSub.iSnapshotCount = rWork.iSnapshotCount;
		rSub.iConfirmedOffset = rWork.iConfirmedOffset;
	}

	// Merge main-thread extrapolation snapshots (only when CRC fast path matched — full replay invalidates them)
	if (rWork.iNewConfirmedTick < 0)
	{
		for (int64_t i = 0; i < iMainSnapshotCount && rSub.iSnapshotCount < engine::kiNetworkBufferSize; ++i)
		{
			if (rWork.snapshots[i] != nullptr && rWork.snapshots[i]->interpolate.iTick > rSub.iConfirmedTick)
			{
				int64_t iPhysical = SnapshotIndex(rSub.iSnapshotHead, rSub.iSnapshotCount);
				rSub.snapshots[iPhysical] = std::move(rWork.snapshots[i]);
				++rSub.iSnapshotCount;
			}
		}
		ASSERT(rSub.iSnapshotCount <= engine::kiNetworkBufferSize);
	}

	RestoreUnconsumedUpdates(rWork, rSub);
}

ReconcileDesyncInfo ClientReconciler::ApplyResult()
{
	// Heap: Frame deserialization, map operations
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	ReconcileContext& rReconcileContext = *mpContext;

	// Handle desync
	if (rReconcileContext.iDesyncTick >= 0)
	{
		ReconcileDesyncInfo desyncInfo;
		desyncInfo.bDesync = true;
		desyncInfo.iDesyncTick = rReconcileContext.iDesyncTick;
		desyncInfo.desyncCoord = rReconcileContext.desyncCoord;
		desyncInfo.desyncExpectedCrc = rReconcileContext.desyncExpectedCrc;
		desyncInfo.desyncActualCrc = rReconcileContext.desyncActualCrc;
		desyncInfo.pDesyncClientFrame = std::move(rReconcileContext.pDesyncClientFrame);

		// Restore per-coord state (moved to context at kick time)
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			auto subscriptionIt = gpGame->mCoordFrames.find(rWork.coord);
			if (subscriptionIt == gpGame->mCoordFrames.end() || subscriptionIt->second.uiGeneration != rWork.uiGeneration)
			{
				continue;
			}
			engine::CoordFrames& rSub = subscriptionIt->second;

			rSub.iConfirmedOffset = rWork.iConfirmedOffset;
			rSub.iSnapshotHead = rWork.iSnapshotHead;

			RestoreUnconsumedUpdates(rWork, rSub);

			// Restore snapshots via swap back
			std::swap(rSub.snapshots, rWork.snapshots);
			rSub.iSnapshotCount = rWork.iSnapshotCount;
		}

		mpContext.reset();
		return desyncInfo;
	}

	// Capture pre-writeback human player position for visual smoothing
	auto GetHumanSnapshotPosition = [](XMVECTOR& rOut) -> bool
	{
		auto subIt = gpGame->mCoordFrames.find(gpGame->mHumanGridCoord);
		if (subIt == gpGame->mCoordFrames.end() || subIt->second.iSnapshotCount <= 0)
			return false;
		int64_t iPhysical = engine::SnapshotIndex(subIt->second.iSnapshotHead, subIt->second.iSnapshotCount - 1);
		const std::unique_ptr<game::Frame>& pSnapshot = subIt->second.snapshots[iPhysical];
		if (pSnapshot == nullptr)
			return false;
		auto oIdx = gpGame->HumanPlayerIndex(*pSnapshot->interpolate.pPlayers);
		if (!oIdx)
			return false;
		rOut = pSnapshot->interpolate.pPlayers->pVecPositions[*oIdx];
		return true;
	};

	XMVECTOR vecPreWritebackPosition {};
	bool bCapturedPrePosition = GetHumanSnapshotPosition(vecPreWritebackPosition);

	// Write back per-coord results
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		auto subscriptionIt = gpGame->mCoordFrames.find(rWork.coord);
		if (subscriptionIt == gpGame->mCoordFrames.end() || subscriptionIt->second.uiGeneration != rWork.uiGeneration)
		{
			continue;
		}
		ApplyCoordWriteback(rWork, subscriptionIt->second);
	}

	// Compute visual error offset from position delta after writeback
	XMVECTOR vecPostWritebackPosition {};
	if (bCapturedPrePosition && rReconcileContext.bAnyFullReplay && GetHumanSnapshotPosition(vecPostWritebackPosition))
	{
		XMVECTOR vecError = XMVectorSubtract(vecPreWritebackPosition, vecPostWritebackPosition);
		XMVECTOR vecTotal = XMVectorAdd(gpGame->mVecVisualErrorOffset, vecError);
		if (XMVectorGetX(XMVector3Length(vecTotal)) > Game::kfVisualErrorMaxDistance)
		{
			gpGame->mVecVisualErrorOffset = {};
		}
		else
		{
			gpGame->mVecVisualErrorOffset = vecTotal;
		}
	}

	// Update human tracking from reconciled state
	mConfirmedHumanState = rReconcileContext.newConfirmedHumanState;
	gpGame->SetPreviousHumanArmor(rReconcileContext.newConfirmedHumanState.fPreviousHumanArmor);

	// Feed reconciliation counters to profile manager
	gpProfileManager->SetReconcileCounters(rReconcileContext.profiling.iCrcValidatedFrameTicks, rReconcileContext.profiling.iAssumedFrameTicks, rReconcileContext.profiling.iCrcFastPathEvents, rReconcileContext.profiling.iStatusChangeReplayTicks, rReconcileContext.profiling.iKnockOnReplayTicks);

	// Advance frame ID counter past worker's usage
	gpGame->SetNextFrameId(std::max(gpGame->NextFrameId(), rReconcileContext.uiNextFrameId));

	if (rReconcileContext.bAnyFullReplay)
	{
		// Restore counters from caught-up state
		gpGame->SetTickCounter(rReconcileContext.iTickCounter);
		gpGame->SetCurrentTime(rReconcileContext.newConfirmedHumanState.fCurrentTime);

		// Ensure next frames exist
		gpGame->EnsureNextFrames();
	}

	mpContext.reset();
	return {};
}

#endif // BT_CLIENT

} // namespace game
