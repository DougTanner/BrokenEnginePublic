#include "Network/Client/ReconcileReplay.h"

#include "Game.h"
#include "Frame/FrameTick.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/StatusChange.h"
#include "Network/Client/ClientReconciler.h"

namespace game
{

#if defined(BT_CLIENT)

static std::unique_ptr<Frame> CloneFrameViaSerialization(const Frame& rFrame)
{
	std::ostringstream outputStream(std::ios::binary);
	outputStream << rFrame;
	std::istringstream inputStream(outputStream.str(), std::ios::binary);
	std::unique_ptr<Frame> pClone = std::make_unique<Frame>();
	inputStream >> *pClone;
	return pClone;
}

void ReconcileInjectPendingFullState(CoordWork& rWork)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	ASSERT(rFrames.pendingFullState->pFrame->interpolate.iTick == rFrames.pendingFullState->iTick);
	int64_t iSlot = SnapshotIndex(rScratch.iReplayWriteHead, rScratch.iReplayWriteCount);
	rFrames.snapshots[iSlot] = std::move(rFrames.pendingFullState->pFrame);
	rScratch.replayStack.clear();
	rScratch.replayStack.push_back(rFrames.snapshots[iSlot].get());
	rScratch.iReplayStackCount = 1;
	rScratch.iReplayWriteHead = SnapshotIndex(iSlot, 1);
	rScratch.iReplayWriteCount = 0;
	// Full state replaces the timeline; a prior higher high-water mark was against a discarded timeline.
	rFrames.iHighWaterValidatedTick = rFrames.pendingFullState->iTick;
	rFrames.iLastFullStateTick = rFrames.pendingFullState->iTick;
	rFrames.pendingFullState.reset();
}

// --- Per-coord reconciliation functions ---

static void ReconcileRollbackCoord(CoordWork& rWork, int64_t iRollbackOffset)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	ASSERT(iRollbackOffset >= 0);
	int64_t iRollbackPhysical = SnapshotIndex(rFrames.iSnapshotHead, iRollbackOffset);
	rScratch.replayStack.clear();
	rScratch.replayStack.push_back(rFrames.snapshots[iRollbackPhysical].get());
	rScratch.iReplayStackCount = 1;
	rScratch.iReplayWriteHead = SnapshotIndex(rFrames.iSnapshotHead, iRollbackOffset + 1);
	rScratch.iReplayWriteCount = 0;
	rScratch.iLastValidatedIndex = -1;
}

static int64_t ReconcileFindReplayRangeCoord(CoordWork& rWork, int64_t iReplayStart)
{
	const engine::CoordFrames& rFrames = *rWork.pFrames;

	int64_t iMaxConsecutive = iReplayStart - 1;
	for (int64_t iTick = iReplayStart; ; ++iTick)
	{
		if (!rFrames.serverUpdates.contains(iTick))
		{
			break;
		}
		iMaxConsecutive = iTick;
	}
	return iMaxConsecutive;
}

static bool ReconcileRunTickCoord(CoordWork& rWork, int64_t iTick, float fTime, FrameInput& rFrameInput, bool bIsReplay)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	// Invariant: any tick whose CRC matched the server must never be re-simulated.
	if (iTick <= rFrames.iHighWaterValidatedTick)
	{
		DEBUG_BREAK();
	}

	if (rScratch.iReplayWriteCount >= engine::kiNetworkBufferSize)
	{
		LOG(kNetwork, kVerbose, "ReconcileRunTickCoord Ring buffer full WriteCount: {} ForTick: {}", rScratch.iReplayWriteCount, iTick);
		return false;
	}

	int64_t iNextSlot = SnapshotIndex(rScratch.iReplayWriteHead, rScratch.iReplayWriteCount);
	if (rFrames.snapshots[iNextSlot] == nullptr)
	{
		rFrames.snapshots[iNextSlot] = std::make_unique<Frame>();
	}

	Frame* pCurrent = rScratch.replayStack[rScratch.iReplayStackCount - 1];
	Frame* pNext = rFrames.snapshots[iNextSlot].get();

	pNext->interpolate.frameFlags.Set(engine::FrameFlags::kRecalculated);

	ActiveFrameRef ref {
		.pNext = pNext,
		.pCurrent = pCurrent,
		.pFrameInput = &rFrameInput,
		.pStaticData = &rFrames.staticData,
	};
	RunFrameTick(ref, iTick, fTime);

	// Apply transfer StatusChanges (runs after Destroy/Spawn to match server ordering)
	bool bHadTransfers = false;
	int64_t iTransferPlayerCount = 0;
	int64_t iTransferBlasterCount = 0;
	int64_t iTransferSpaceshipCount = 0;
	int64_t iTransferMissileCount = 0;
	engine::global_id_t transferPlayerIds[8] {};
	for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
	{
		if (IsTransferType(rStatusChange.eType))
		{
			const TransferData& rData = std::get<TransferData>(rStatusChange.data);
			SpawnTransfer(*pNext, rStatusChange.eType, rData, pNext->postRender.playerAlignment);
			bHadTransfers = true;

			switch (rStatusChange.eType)
			{
				case StatusChangeType::kTransferPlayer:
					if (iTransferPlayerCount < 8)
					{
						transferPlayerIds[iTransferPlayerCount] = rData.globalPlayerId;
					}
					++iTransferPlayerCount;
					break;
				case StatusChangeType::kTransferBlaster:   ++iTransferBlasterCount;   break;
				case StatusChangeType::kTransferSpaceship: ++iTransferSpaceshipCount; break;
				case StatusChangeType::kTransferMissile:   ++iTransferMissileCount;   break;
				default: break;
			}
		}
	}
	if (bHadTransfers && !bIsReplay && iTick > rWork.pFrames->iLastSpawnTransferLogTick)
	{
		rWork.pFrames->iLastSpawnTransferLogTick = iTick;
		if (iTransferPlayerCount > 0)
		{
			int64_t iLogCount = std::min(iTransferPlayerCount, int64_t {8});
			char acPlayerIds[192] {};
			int64_t iPos = 0;
			for (int64_t i = 0; i < iLogCount; ++i)
			{
				if (i > 0) { acPlayerIds[iPos++] = ','; acPlayerIds[iPos++] = ' '; }
				iPos += snprintf(acPlayerIds + iPos, sizeof(acPlayerIds) - iPos, "%lld", transferPlayerIds[i].iValue);
			}
			LOG(kNetwork, kVerbose, "ReconcileRunTickCoord SpawnTransfers Coord: ({},{}) ForTick: {} TransferCount: {} PlayerCount: {} BlasterCount: {} SpaceshipCount: {} MissileCount: {} PlayerIds: [{}]", rWork.coord.x, rWork.coord.y, iTick, iTransferPlayerCount + iTransferBlasterCount + iTransferSpaceshipCount + iTransferMissileCount, iTransferPlayerCount, iTransferBlasterCount, iTransferSpaceshipCount, iTransferMissileCount, acPlayerIds);
		}
		else
		{
			LOG(kNetwork, kVerbose, "ReconcileRunTickCoord SpawnTransfers Coord: ({},{}) ForTick: {} TransferCount: {} PlayerCount: {} BlasterCount: {} SpaceshipCount: {} MissileCount: {}", rWork.coord.x, rWork.coord.y, iTick, iTransferPlayerCount + iTransferBlasterCount + iTransferSpaceshipCount + iTransferMissileCount, iTransferPlayerCount, iTransferBlasterCount, iTransferSpaceshipCount, iTransferMissileCount);
		}
	}
	std::erase_if(rFrameInput.statusChanges, [](const StatusChange& rStatusChange)
	{
		return IsTransferType(rStatusChange.eType);
	});

	if (bHadTransfers)
	{
		pNext->postRender.sharedCrc = pNext->Crcs();
	}

	// Advance replay stack
	rScratch.replayStack.push_back(pNext);
	++rScratch.iReplayStackCount;
	++rScratch.iReplayWriteCount;

	return true;
}

static bool ReconcileValidateCrcCoord(CoordWork& rWork, int64_t iTick, const engine::CoordFrames::CoordServerUpdate& rUpdate, const FrameInput& rFrameInput)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	Frame& rCurrentFrame = *rScratch.replayStack[rScratch.iReplayStackCount - 1];
	rCurrentFrame.interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
	common::crc_t clientCrc = rCurrentFrame.postRender.sharedCrc;

	if (clientCrc != rUpdate.sharedCrc)
	{
		if (!rScratch.bSuppressRepeatLogs)
		{
			char acSharedCrc[20] {}, acClientCrc[20] {};
			common::ToHex(std::span<char, 20>(acSharedCrc), rUpdate.sharedCrc);
			common::ToHex(std::span<char, 20>(acClientCrc), clientCrc);
			LOG(kNetwork, kVerbose, "ReconcileValidateCrcCoord Desync Coord: ({},{}) ForTick: {} ServerCrc: {} ClientCrc: {} ServerStatusChanges: {} ClientStatusChanges: {}", rWork.coord.x, rWork.coord.y, iTick, acSharedCrc, acClientCrc, rUpdate.statusChanges.size(), rFrameInput.statusChanges.size());
		}

		rScratch.iDesyncTick = iTick;
		rScratch.desyncExpectedCrc = rUpdate.sharedCrc;
		rScratch.desyncActualCrc = clientCrc;
		rScratch.pDesyncClientFrame = CloneFrameViaSerialization(rCurrentFrame);
		return false;
	}

	// Record CRC-validated index
	rScratch.iLastValidatedIndex = rScratch.iReplayStackCount - 1;
	rScratch.iNewConfirmedTick = iTick;
	rFrames.iHighWaterValidatedTick = std::max(rFrames.iHighWaterValidatedTick, iTick);

	return true;
}

static void ReconcileReplayCoord(CoordWork& rWork, const ReconcileInputs& rInputs, int64_t iReplayStart, int64_t iRollbackOffset, int64_t iMaxConsecutive, float& rfTime)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	int64_t iAvailable = iMaxConsecutive - (iReplayStart - 1);
	static constexpr int64_t kiLowJitterThresholdUs = 2000;
	static constexpr int64_t kiHighJitterThresholdUs = 8000;
	int64_t iJitterUs = rInputs.iJitterUs;
	int64_t iMaxReplay = 0;
	if (iJitterUs <= kiLowJitterThresholdUs)
	{
		iMaxReplay = iAvailable;
	}
	else if (iJitterUs >= kiHighJitterThresholdUs)
	{
		iMaxReplay = std::max<int64_t>(iAvailable / 4, 1);
	}
	else
	{
		iMaxReplay = std::max<int64_t>(iAvailable / 2, 1);
	}
	// Gap-aware override: when backlog is large, allow more replay to prevent cascading failure
	int64_t iGap = rInputs.iTargetTick - rFrames.iConfirmedTick;
	static constexpr int64_t kiGapOverrideThreshold = engine::kiNetworkBufferSize / 2;
	if (iGap >= kiGapOverrideThreshold)
	{
		static constexpr int64_t kiRingBudget = engine::kiNetworkBufferSize * 3 / 4;
		iMaxReplay = std::max(iMaxReplay, std::min(iAvailable, kiRingBudget));
	}
	bool bGapOverride = iGap >= kiGapOverrideThreshold;
	if (std::abs(iMaxReplay - rFrames.iLastLoggedMaxReplay) > 1 || bGapOverride != rFrames.bLastLoggedGapOverride)
	{
		LOG(kNetwork, kVerbose, "ReconcileReplayCoord Throttle Coord: ({},{}) JitterUs: {} Available: {} MaxReplay: {} Gap: {} GapOverride: {}", rWork.coord.x, rWork.coord.y, iJitterUs, iAvailable, iMaxReplay, iGap, bGapOverride);
		rFrames.iLastLoggedMaxReplay = iMaxReplay;
		rFrames.bLastLoggedGapOverride = bGapOverride;
	}
	int64_t iReplayCount = 0;

	for (int64_t iTick = iReplayStart; iTick <= iMaxConsecutive; ++iTick)
	{
		if (iReplayCount >= iMaxReplay)
		{
			break;
		}

		auto updateIt = rFrames.serverUpdates.find(iTick);
		if (updateIt == rFrames.serverUpdates.end())
		{
			break;
		}

		rfTime += kfDeltaTime;

		if (iTick <= rScratch.iPreReconcileTailTick)
		{
			rScratch.bReSimOccurred = true;
		}

		FrameInput frameInput;
		frameInput.statusChanges = updateIt->second.statusChanges;

		if (!ReconcileRunTickCoord(rWork, iTick, rfTime, frameInput, true))
		{
			break;
		}

		// Inject pending full state at matching tick
		if (rFrames.pendingFullState.has_value() && rFrames.pendingFullState->iTick == iTick)
		{
			ReconcileInjectPendingFullState(rWork);
			LOG(kNetwork, kVerbose, "ReconcileReplayCoord Injected pending full state Coord: ({},{}) ForTick: {}", rWork.coord.x, rWork.coord.y, iTick);
		}

		// CRC validation
		if (!ReconcileValidateCrcCoord(rWork, iTick, updateIt->second, frameInput))
		{
			return;
		}

		// Profiling
		bool bHadStatusChanges = !updateIt->second.statusChanges.empty();

		// Consume server update
		rFrames.serverUpdates.erase(updateIt);

		if (bHadStatusChanges)
		{
			++rScratch.profiling.iStatusChangeReplayTicks;
		}
		else
		{
			++rScratch.profiling.iKnockOnReplayTicks;
		}

		++iReplayCount;
	}

	// Record physical ring index of new confirmed frame
	if (rScratch.iLastValidatedIndex >= 0)
	{
		if (rScratch.iLastValidatedIndex == 0)
		{
			rScratch.iNewConfirmedOffset = SnapshotIndex(rFrames.iSnapshotHead, iRollbackOffset);
		}
		else
		{
			rScratch.iNewConfirmedOffset = SnapshotIndex(rScratch.iReplayWriteHead, rScratch.iLastValidatedIndex - 1);
		}
	}
}

// Simulate one forward tick. If serverUpdates has an entry for iTick, fold its StatusChanges
// into the FrameInput so forward sim produces the server-correct state (CRC will match on the
// next Reconcile's fast path). Does not validate, advance iConfirmedTick, or erase the entry —
// all promotion happens on the next frame via the existing CRC fast path.
static bool ReconcileForwardStepCoord(CoordWork& rWork, int64_t iTick, float& rfTime)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	rfTime += kfDeltaTime;

	FrameInput frameInput;
	auto updateIt = rFrames.serverUpdates.find(iTick);
	if (updateIt != rFrames.serverUpdates.end())
	{
		frameInput.statusChanges = updateIt->second.statusChanges;
	}

	if (!ReconcileRunTickCoord(rWork, iTick, rfTime, frameInput, false))
	{
		return false;
	}

	++rScratch.profiling.iAssumedFrameTicks;
	return true;
}

static void ReconcileCatchUpCoord(CoordWork& rWork, int64_t iTargetTick, float& rfTime)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	int64_t iStartWriteCount = rScratch.iReplayWriteCount;
	int64_t iCurrentTick = rScratch.replayStack[rScratch.iReplayStackCount - 1]->interpolate.iTick;

	int64_t iBudget = engine::kiNetworkBufferSize - rScratch.iReplayWriteCount;
	int64_t iCappedTarget = std::min(iTargetTick, iCurrentTick + iBudget);

	while (iCurrentTick < iCappedTarget)
	{
		++iCurrentTick;
		if (!ReconcileForwardStepCoord(rWork, iCurrentTick, rfTime))
		{
			break;
		}
	}

	// Clear recalculated flag on catch-up frames (replay frames keep it for rendering)
	for (int64_t i = iStartWriteCount; i < rScratch.iReplayWriteCount; ++i)
	{
		int64_t iSlot = SnapshotIndex(rScratch.iReplayWriteHead, i);
		rFrames.snapshots[iSlot]->interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
	}
}

// Fast-path catch-up: extends the ring tail forward to iTargetTick using empty-input sim.
// Called after the CRC fast path commits, so the ring contains [confirmed, ...speculative tail].
// The tail may be behind iTargetTick because miTickCounter advanced this frame; we append new
// catch-up frames starting from the tail and grow iSnapshotCount in place.
static void ReconcileFastPathCatchUp(CoordWork& rWork, int64_t iTargetTick)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	if (rFrames.iSnapshotCount == 0)
	{
		return;
	}

	int64_t iTailOffset = rFrames.iSnapshotCount - 1;
	int64_t iTailPhysical = SnapshotIndex(rFrames.iSnapshotHead, iTailOffset);
	Frame* pTail = rFrames.snapshots[iTailPhysical].get();
	if (pTail == nullptr || pTail->interpolate.iTick >= iTargetTick)
	{
		return;
	}

	rScratch.replayStack.clear();
	rScratch.replayStack.push_back(pTail);
	rScratch.iReplayStackCount = 1;
	rScratch.iReplayWriteHead = SnapshotIndex(iTailPhysical, 1);
	rScratch.iReplayWriteCount = 0;

	float fTime = pTail->interpolate.fCurrentTime;
	int64_t iStartCount = rFrames.iSnapshotCount;
	int64_t iBudget = engine::kiNetworkBufferSize - iStartCount;
	int64_t iCurrentTick = pTail->interpolate.iTick;
	int64_t iCappedTarget = std::min(iTargetTick, iCurrentTick + iBudget);

	while (iCurrentTick < iCappedTarget)
	{
		++iCurrentTick;
		if (!ReconcileForwardStepCoord(rWork, iCurrentTick, fTime))
		{
			break;
		}
	}

	for (int64_t i = 0; i < rScratch.iReplayWriteCount; ++i)
	{
		int64_t iSlot = SnapshotIndex(rScratch.iReplayWriteHead, i);
		rFrames.snapshots[iSlot]->interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
	}

	rFrames.iSnapshotCount = std::min(
		iStartCount + rScratch.iReplayWriteCount,
		static_cast<int64_t>(engine::kiNetworkBufferSize));
}

// Apply scratch output to rFrames in-place — writeback runs inside the same dispatch worker
// since the scratch already mutated rFrames fields (iHighWaterValidatedTick, iConfirmedTick via
// fast path, serverUpdates erase, snapshot slot allocation). This routine commits the final
// ring layout (iConfirmedTick/iSnapshotHead/iConfirmedOffset/iSnapshotCount) for success paths.
static void ApplyCoordWriteback(CoordWork& rWork)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	if (rScratch.iNewConfirmedTick >= 0)
	{
		rFrames.iConfirmedTick = rScratch.iNewConfirmedTick;
		rFrames.iSnapshotHead = rScratch.iNewConfirmedOffset;
		// iNewConfirmedInnerOffset is nonzero only when the fast-path retained frames before
		// confirmed for render-behind; replay/rollback paths leave it 0 (head == confirmed).
		rFrames.iConfirmedOffset = rScratch.iNewConfirmedInnerOffset;
		rFrames.iSnapshotCount = rScratch.iOutputCount;
		ASSERT(rFrames.iSnapshotCount >= 0 && rFrames.iSnapshotCount <= engine::kiNetworkBufferSize);
	}
}

void ReconcileCoord(CoordWork& rWork, const ReconcileInputs& rInputs)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	// Capture pre-reconcile ring tail tick so ReconcileReplayCoord can distinguish actual
	// re-simulation (replay of a tick that already existed) from first-time forward sim.
	if (rFrames.iSnapshotCount > 0)
	{
		int64_t iTailPhysical = SnapshotIndex(rFrames.iSnapshotHead, rFrames.iSnapshotCount - 1);
		if (rFrames.snapshots[iTailPhysical] != nullptr)
		{
			rScratch.iPreReconcileTailTick = rFrames.snapshots[iTailPhysical]->interpolate.iTick;
		}
	}

	// Aggressive CRC walk: finds the highest matching ring frame across all server updates
	// in range, advances iConfirmedTick/iConfirmedOffset to it, and reports the lowest
	// unresolved mismatch (if any) past the new confirmed point.
	CrcFastPathCoordResult fastPathResult = CrcFastPathProcessCoord(rWork, rInputs.iTargetTick);
	if (fastPathResult.bHandled)
	{
		++rScratch.profiling.iCrcFastPathEvents;
		rFrames.iLastReplayConfirmedTick = -1;
		rFrames.iLastReplayServerUpdateCount = -1;
		ApplyCoordWriteback(rWork);
		ReconcileFastPathCatchUp(rWork, rInputs.iTargetTick);
		return;
	}

	// The walk may have set iNewConfirmedTick/iNewConfirmedOffset via CrcApplyMatchResult —
	// preserve those as the floor result. If full replay validates further, ReconcileValidateCrcCoord
	// and ReconcileReplayCoord will overwrite them. iOutputCount must be recomputed from scratch
	// because walk's count included old speculative frames that replay will overwrite.
	rScratch.bCrcFastPath = false;
	rScratch.iOutputCount = 0;

	// No server data at the first tick past confirmed — replay cannot start.
	// Keep existing speculative ring (populated by prior catch-up) and wait for resend.
	if (!rFrames.serverUpdates.contains(rFrames.iConfirmedTick + 1) && !rFrames.pendingFullState.has_value())
	{
		if (rScratch.iNewConfirmedTick >= 0)
		{
			// rFrames.iConfirmedOffset was mutated by CrcValidateCoord to iHighestMatchIndex
			// (offset from OLD head to confirmed). Retention shifts the new head back by
			// kiRenderBehindTicks slots so the renderer retains a prev-tail — iOutputCount
			// must count from the retained head, not from confirmed.
			int64_t iHeadAdvance = std::max<int64_t>(0, rFrames.iConfirmedOffset - engine::kiRenderBehindTicks);
			rScratch.iOutputCount = rFrames.iSnapshotCount - iHeadAdvance;
			ApplyCoordWriteback(rWork);
		}
		ReconcileFastPathCatchUp(rWork, rInputs.iTargetTick);
		return;
	}

	rScratch.bReplayed = true;

	// Invariant: full replay must not repeat identical work. If iConfirmedTick and serverUpdates
	// are unchanged since the last full replay attempt, the result would be the same.
	int64_t iCurrentUpdateCount = static_cast<int64_t>(rFrames.serverUpdates.size());
	if (rFrames.iConfirmedTick == rFrames.iLastReplayConfirmedTick
		&& iCurrentUpdateCount == rFrames.iLastReplayServerUpdateCount
		&& !rFrames.pendingFullState.has_value())
	{
		DEBUG_BREAK();
	}
	rFrames.iLastReplayConfirmedTick = rFrames.iConfirmedTick;
	rFrames.iLastReplayServerUpdateCount = iCurrentUpdateCount;

	// Determine rollback base: prefer the shrunk target (one tick before the lowest unresolved
	// mismatch), using the speculative ring frame at that logical offset as the starting state.
	// Fall back to iConfirmedTick if the shrunk target is unavailable or the walk didn't find
	// a mismatch (e.g., pending full state or gap-only path).
	// Only attempt shrunk rollback if the base frame was CRC-validated — speculative frames
	// from catch-up are guaranteed wrong when a gap caused the mismatch.
	int64_t iRollbackTick = rFrames.iConfirmedTick;
	int64_t iRollbackOffset = rFrames.iConfirmedOffset;
	bool bShrunkRollback = false;
	if (fastPathResult.iLowestUnresolvedMismatch > rFrames.iConfirmedTick + 1 && !rFrames.pendingFullState.has_value())
	{
		int64_t iShrunkTick = fastPathResult.iLowestUnresolvedMismatch - 1;
		if (iShrunkTick <= rFrames.iHighWaterValidatedTick)
		{
			int64_t iShrunkIndex = -1;
			for (int64_t i = 0; i < rFrames.iSnapshotCount; ++i)
			{
				int64_t iPhysical = SnapshotIndex(rFrames.iSnapshotHead, i);
				if (rFrames.snapshots[iPhysical] != nullptr && rFrames.snapshots[iPhysical]->interpolate.iTick == iShrunkTick)
				{
					iShrunkIndex = i;
					break;
				}
			}
			if (iShrunkIndex >= 0)
			{
				iRollbackTick = iShrunkTick;
				iRollbackOffset = iShrunkIndex;
				bShrunkRollback = true;
				rScratch.bShrunkRollback = true;
			}
		}
	}

	ReconcileRollbackCoord(rWork, iRollbackOffset);
	float fTime = rScratch.replayStack[0]->interpolate.fCurrentTime;

	// Inject pending full state at confirmed frame (stale states rejected at receive time).
	// Only applies to the full-rollback path — shrunk rollback disables this branch above.
	if (rFrames.pendingFullState.has_value() && rFrames.pendingFullState->iTick == rFrames.iConfirmedTick)
	{
		ReconcileInjectPendingFullState(rWork);
		fTime = rScratch.replayStack[0]->interpolate.fCurrentTime;
		LOG(kNetwork, kVerbose, "ReconcileCoord Injected pending full state Coord: ({},{}) AtTick: {}", rWork.coord.x, rWork.coord.y, rFrames.iConfirmedTick);
	}
	else if (rFrames.pendingFullState.has_value() && rFrames.pendingFullState->iTick < rFrames.iConfirmedTick)
	{
		LOG(kNetwork, kVerbose, "ReconcileCoord Discarded stale pending full state Coord: ({},{}) FullStateTick: {} ConfirmedTick: {}", rWork.coord.x, rWork.coord.y, rFrames.pendingFullState->iTick, rFrames.iConfirmedTick);
		rFrames.pendingFullState.reset();
	}

	int64_t iReplayStart = iRollbackTick + 1;
	int64_t iMaxConsecutive = std::min(ReconcileFindReplayRangeCoord(rWork, iReplayStart), rInputs.iTargetTick);

	ReconcileReplayCoord(rWork, rInputs, iReplayStart, iRollbackOffset, iMaxConsecutive, fTime);

	// Two-tier rollback fallback: if shrunk rollback desynced at the very first replay tick,
	// the speculative starting state was bad. Clear the desync, reset replay state, and retry
	// with a full rollback to iConfirmedTick.
	if (rScratch.iDesyncTick >= 0 && bShrunkRollback && rScratch.iDesyncTick == iReplayStart)
	{
		if (!rScratch.bSuppressRepeatLogs)
		{
			LOG(kNetwork, kVerbose, "ReconcileCoord Shrunk rollback failed Coord: ({},{}) DesyncTick: {} — falling back to full rollback", rWork.coord.x, rWork.coord.y, rScratch.iDesyncTick);
		}
		rScratch.iDesyncTick = -1;
		rScratch.desyncExpectedCrc = 0;
		rScratch.desyncActualCrc = 0;
		rScratch.pDesyncClientFrame.reset();
		rScratch.iLastValidatedIndex = -1;

		iRollbackTick = rFrames.iConfirmedTick;
		iRollbackOffset = rFrames.iConfirmedOffset;
		bShrunkRollback = false;
		rScratch.bShrunkRollback = false;

		ReconcileRollbackCoord(rWork, iRollbackOffset);
		fTime = rScratch.replayStack[0]->interpolate.fCurrentTime;

		if (rFrames.pendingFullState.has_value() && rFrames.pendingFullState->iTick == rFrames.iConfirmedTick)
		{
			ReconcileInjectPendingFullState(rWork);
			fTime = rScratch.replayStack[0]->interpolate.fCurrentTime;
		}

		iReplayStart = iRollbackTick + 1;
		iMaxConsecutive = std::min(ReconcileFindReplayRangeCoord(rWork, iReplayStart), rInputs.iTargetTick);

		// Safety net: if full rollback range is also empty, skip replay + catch-up entirely.
		if (iMaxConsecutive < iReplayStart && !rFrames.pendingFullState.has_value())
		{
			if (rScratch.iNewConfirmedTick >= 0)
			{
				// See the matching block above: retention shifts the head back by
				// kiRenderBehindTicks slots. iOutputCount must count from retained head.
				int64_t iHeadAdvance = std::max<int64_t>(0, rFrames.iConfirmedOffset - engine::kiRenderBehindTicks);
				rScratch.iOutputCount = rFrames.iSnapshotCount - iHeadAdvance;
				ApplyCoordWriteback(rWork);
			}
			ReconcileFastPathCatchUp(rWork, rInputs.iTargetTick);
			return;
		}

		ReconcileReplayCoord(rWork, rInputs, iReplayStart, iRollbackOffset, iMaxConsecutive, fTime);
	}

	if (rScratch.iDesyncTick >= 0)
	{
		return;
	}

	ReconcileCatchUpCoord(rWork, rInputs.iTargetTick, fTime);

	// Compute output layout: confirmed frame + remaining replay/catch-up frames
	if (rScratch.iLastValidatedIndex > 0)
	{
		rScratch.iOutputCount = rScratch.iReplayWriteCount - (rScratch.iLastValidatedIndex - 1);
	}
	else if (rScratch.iLastValidatedIndex == 0)
	{
		rScratch.iOutputCount = rScratch.iReplayWriteCount + 1;
	}
	else if (rScratch.iNewConfirmedTick >= 0)
	{
		// Walk advanced iConfirmedTick but full replay didn't validate anything further.
		// Preserve walk's confirmed frame as the base and include new catch-up frames.
		rScratch.iOutputCount = rScratch.iReplayWriteCount + 1;
	}
	else
	{
		// Full replay ran catch-up without validating (gap in serverUpdates past confirmed).
		// Preserve existing confirmed tick/offset as the base so catch-up frames are committed.
		rScratch.iNewConfirmedTick = rFrames.iConfirmedTick;
		rScratch.iNewConfirmedOffset = SnapshotIndex(rFrames.iSnapshotHead, iRollbackOffset);
		rScratch.iOutputCount = rScratch.iReplayWriteCount + 1;
	}

	rScratch.iOutputCount = std::min(rScratch.iOutputCount, static_cast<int64_t>(engine::kiNetworkBufferSize));
	ASSERT(rScratch.iOutputCount >= 0 && rScratch.iOutputCount <= engine::kiNetworkBufferSize);

	rScratch.iTickCounter = rInputs.iTargetTick;
	ASSERT(rScratch.replayStack[rScratch.iReplayStackCount - 1]->interpolate.iTick <= rInputs.iTargetTick);
	rScratch.fCurrentTime = fTime;

	ApplyCoordWriteback(rWork);
}

#endif // BT_CLIENT

} // namespace game
