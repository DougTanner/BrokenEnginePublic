#include "Game.h"

#include "Network/ClientReconciler.h"
#include "Network/ReconcileReplay.h"
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
	rReconcileContext.playerAlignment = gpGame->PlayerAlignment();
	rReconcileContext.alignments = gpGame->Alignments();

	// Dispatch to worker
	mpWorker->Wake([this]()
	{
		Reconcile(*mpContext, mpContext->alignments);
	});
}

void ClientReconciler::Reconcile(ReconcileContext& rReconcileContext, [[maybe_unused]] const engine::Alignments& rAlignments)
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		ReconcileCoord(rReconcileContext, rWork);
		if (rReconcileContext.iDesyncTick >= 0)
		{
			return;
		}
	}

	ReconcileUpdateHumanState(rReconcileContext);
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
	}
	else
	{
		rSub.iSnapshotHead = rWork.iSnapshotHead;
		rSub.iSnapshotCount = rWork.iSnapshotCount;
		rSub.iConfirmedOffset = rWork.iConfirmedOffset;
	}

	// Merge main-thread extrapolation snapshots
	for (int64_t i = 0; i < iMainSnapshotCount && rSub.iSnapshotCount < engine::kiNetworkBufferSize; ++i)
	{
		if (rWork.snapshots[i] != nullptr && rWork.snapshots[i]->interpolate.iTick > rSub.iConfirmedTick)
		{
			int64_t iPhysical = SnapshotIndex(rSub.iSnapshotHead, rSub.iSnapshotCount);
			rSub.snapshots[iPhysical] = std::move(rWork.snapshots[i]);
			++rSub.iSnapshotCount;
		}
	}

	// Restore unconsumed server updates
	for (auto& [iTick, rUpdate] : rWork.serverUpdates)
	{
		if (iTick > rSub.iConfirmedTick)
		{
			rSub.serverUpdates.insert_or_assign(iTick, std::move(rUpdate));
		}
	}
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
		desyncInfo.desyncServerCrc = rReconcileContext.desyncServerCrc;
		desyncInfo.desyncClientCrc = rReconcileContext.desyncClientCrc;
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

			for (auto& [iTick, rUpdate] : rWork.serverUpdates)
			{
				if (iTick > rSub.iConfirmedTick)
				{
					rSub.serverUpdates.insert_or_assign(iTick, std::move(rUpdate));
				}
			}

			// Restore snapshots via swap back
			std::swap(rSub.snapshots, rWork.snapshots);
			rSub.iSnapshotCount = rWork.iSnapshotCount;
		}

		mpContext.reset();
		return desyncInfo;
	}

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
