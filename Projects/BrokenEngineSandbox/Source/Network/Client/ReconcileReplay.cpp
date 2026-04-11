#include "Network/Client/ReconcileReplay.h"

#include "Game.h"
#include "Frame/FrameTick.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
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

void ReconcileInjectPendingFullState(CoordReconcileWork& rWork)
{
	ASSERT(rWork.pendingFullState->pFrame->interpolate.iTick == rWork.pendingFullState->iTick);
	int64_t iSlot = SnapshotIndex(rWork.iReplayWriteHead, rWork.iReplayWriteCount);
	rWork.snapshots[iSlot] = std::move(rWork.pendingFullState->pFrame);
	rWork.replayStack.clear();
	rWork.replayStack.push_back(rWork.snapshots[iSlot].get());
	rWork.iReplayStackCount = 1;
	rWork.iReplayWriteHead = SnapshotIndex(iSlot, 1);
	rWork.iReplayWriteCount = 0;
	// Full state replaces the timeline; a prior higher high-water mark was against a discarded timeline.
	rWork.iHighWaterValidatedTick = rWork.pendingFullState->iTick;
	rWork.pendingFullState.reset();
}

// --- Per-coord reconciliation functions ---

static void ReconcileRollbackCoord(CoordReconcileWork& rWork, int64_t iRollbackOffset)
{
	ASSERT(iRollbackOffset >= 0);
	int64_t iRollbackPhysical = SnapshotIndex(rWork.iSnapshotHead, iRollbackOffset);
	rWork.replayStack.clear();
	rWork.replayStack.push_back(rWork.snapshots[iRollbackPhysical].get());
	rWork.iReplayStackCount = 1;
	rWork.iReplayWriteHead = SnapshotIndex(rWork.iSnapshotHead, iRollbackOffset + 1);
	rWork.iReplayWriteCount = 0;
	rWork.iLastValidatedIndex = -1;
}

static int64_t ReconcileFindReplayRangeCoord(CoordReconcileWork& rWork, int64_t iReplayStart)
{
	int64_t iMaxConsecutive = iReplayStart - 1;
	for (int64_t iTick = iReplayStart; ; ++iTick)
	{
		if (!rWork.serverUpdates.contains(iTick))
		{
			break;
		}
		iMaxConsecutive = iTick;
	}
	return iMaxConsecutive;
}

static bool ReconcileRunTickCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork, int64_t iTick, float fTime, FrameInput& rFrameInput)
{
	// Invariant: any tick whose CRC matched the server must never be re-simulated.
	if (iTick <= rWork.iHighWaterValidatedTick)
	{
		DEBUG_BREAK();
	}

	if (rWork.iReplayWriteCount >= engine::kiNetworkBufferSize)
	{
		LOG(kNetwork, kVerbose, "ReconcileRunTickCoord Ring buffer full WriteCount: {} Tick: {}", rWork.iReplayWriteCount, iTick);
		return false;
	}

	int64_t iNextSlot = SnapshotIndex(rWork.iReplayWriteHead, rWork.iReplayWriteCount);
	if (rWork.snapshots[iNextSlot] == nullptr)
	{
		rWork.snapshots[iNextSlot] = std::make_unique<Frame>();
	}

	Frame* pCurrent = rWork.replayStack[rWork.iReplayStackCount - 1];
	Frame* pNext = rWork.snapshots[iNextSlot].get();

	pNext->interpolate.frameFlags.Set(engine::FrameFlags::kRecalculated);

	ActiveFrameRef ref {
		.pNext = pNext,
		.pCurrent = pCurrent,
		.pFrameInput = &rFrameInput,
		.pStaticData = &rWork.staticData,
	};
	RunFrameTick(ref, iTick, fTime);

	// Logs 2+4: pre-transfer sibling-coord snapshot + client transfer ordering,
	// gated on any transfer-type status change being present in this coord-tick.
	int64_t iTransferCount = 0;
	for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
	{
		if (IsTransferType(rStatusChange.eType))
		{
			++iTransferCount;
		}
	}
	if (iTransferCount > 0)
	{
		// Single workbuffer scope holds both Types and Siblings separated by a
		// sentinel, since nested Push/Pop scopes cannot both be viewed at once
		// (Workbuffer::View returns only the innermost scope).
		// Sibling iConfirmedTick/iReplayStackCount are read lock-free during
		// parallel reconcile dispatch; values are best-effort snapshots. int64_t
		// reads are atomic on x86_64 so no tearing, but ordering across coords
		// is non-deterministic — that is the signal we want.
		common::ScopedWorkbufferBuilder builder(common::gpThreadLocal->mWorkbuffer);
		builder.Append("[");
		bool bFirstType = true;
		for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
		{
			if (!IsTransferType(rStatusChange.eType))
			{
				continue;
			}
			if (!bFirstType)
			{
				builder.Append(",");
			}
			bFirstType = false;
			builder.Append(StatusChangeTypeName(rStatusChange.eType));
		}
		builder.Append("] Siblings: [");
		bool bFirstSibling = true;
		for (const CoordReconcileWork& rSibling : rReconcileContext.coordWork)
		{
			if (&rSibling == &rWork)
			{
				continue;
			}
			if (!bFirstSibling)
			{
				builder.Append(" ");
			}
			bFirstSibling = false;
			builder.Append("(");
			builder.Append(static_cast<int64_t>(rSibling.coord.x));
			builder.Append(",");
			builder.Append(static_cast<int64_t>(rSibling.coord.y));
			builder.Append(")=C");
			builder.Append(rSibling.iConfirmedTick);
			builder.Append("/T");
			builder.Append(rSibling.iConfirmedTick + rSibling.iReplayStackCount - 1);
		}
		builder.Append("]");

		LOG(kNetwork, kVerbose, "SpawnTransfer sibling snapshot Coord: ({},{}) Tick: {} TransferCount: {} TransferInfo: Types: {}", rWork.coord.x, rWork.coord.y, iTick, iTransferCount, builder);
	}

	// Apply transfer StatusChanges (runs after Destroy/Spawn to match server ordering)
	bool bHadTransfers = false;
	for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
	{
		if (IsTransferType(rStatusChange.eType))
		{
			// Log 3: destination coord contents before/after this spawn.
			int64_t iPreCount = 0;
			switch (rStatusChange.eType)
			{
				case StatusChangeType::kTransferPlayer:    iPreCount = pNext->postRender.pPlayers->iCount; break;
				case StatusChangeType::kTransferBlaster:   iPreCount = pNext->postRender.pBlasters->iCount; break;
				case StatusChangeType::kTransferSpaceship: iPreCount = pNext->postRender.pSpaceships->iCount; break;
				case StatusChangeType::kTransferMissile:   iPreCount = pNext->postRender.pMissiles->iCount; break;
				default: break;
			}

			SpawnTransfer(*pNext, rStatusChange.eType, std::get<TransferData>(rStatusChange.data), pNext->postRender.playerAlignment);
			bHadTransfers = true;

			int64_t iPostCount = 0;
			switch (rStatusChange.eType)
			{
				case StatusChangeType::kTransferPlayer:    iPostCount = pNext->postRender.pPlayers->iCount; break;
				case StatusChangeType::kTransferBlaster:   iPostCount = pNext->postRender.pBlasters->iCount; break;
				case StatusChangeType::kTransferSpaceship: iPostCount = pNext->postRender.pSpaceships->iCount; break;
				case StatusChangeType::kTransferMissile:   iPostCount = pNext->postRender.pMissiles->iCount; break;
				default: break;
			}

			int64_t iGlobalPlayerId = (rStatusChange.eType == StatusChangeType::kTransferPlayer)
				? std::get<TransferData>(rStatusChange.data).globalPlayerId.iValue
				: 0;
			LOG(kNetwork, kVerbose, "SpawnTransfer applied Coord: ({},{}) Tick: {} Type: {} PreCount: {} PostCount: {} Player[globalId]: {} StatusChangeCount: {}", rWork.coord.x, rWork.coord.y, iTick, StatusChangeTypeName(rStatusChange.eType), iPreCount, iPostCount, iGlobalPlayerId, rFrameInput.statusChanges.size());
		}
	}
	std::erase_if(rFrameInput.statusChanges, [](const StatusChange& rStatusChange)
	{
		return IsTransferType(rStatusChange.eType);
	});

	if (bHadTransfers)
	{
		pNext->postRender.sharedCrc = pNext->Crcs();
		pNext->postRender.previousInputCrc = rFrameInput.ServerInputCrc();
	}

	// Advance replay stack
	rWork.replayStack.push_back(pNext);
	++rWork.iReplayStackCount;
	++rWork.iReplayWriteCount;

	return true;
}

static bool ReconcileValidateCrcCoord([[maybe_unused]] ReconcileContext& rReconcileContext, CoordReconcileWork& rWork, int64_t iTick, const engine::CoordFrames::CoordServerUpdate& rUpdate, const FrameInput& rFrameInput)
{
	Frame& rCurrentFrame = *rWork.replayStack[rWork.iReplayStackCount - 1];
	rCurrentFrame.interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
	common::crc_t clientCrc = rCurrentFrame.postRender.sharedCrc;

	if (clientCrc != rUpdate.sharedCrc)
	{
		char acSharedCrc[20] {}, acClientCrc[20] {}, acServerInputCrc[20] {}, acClientInputCrc[20] {};
		common::ToHex(std::span<char, 20>(acSharedCrc), rUpdate.sharedCrc);
		common::ToHex(std::span<char, 20>(acClientCrc), clientCrc);
		common::ToHex(std::span<char, 20>(acServerInputCrc), rUpdate.inputCrc);
		common::ToHex(std::span<char, 20>(acClientInputCrc), rFrameInput.ServerInputCrc());
		LOG(kNetwork, kVerbose, "ReconcileValidateCrcCoord Desync Coord: ({},{}) Tick: {} ServerCrc: {} ClientCrc: {} ServerInputCrc: {} ClientInputCrc: {} ServerStatusChanges: {} ClientStatusChanges: {}", rWork.coord.x, rWork.coord.y, iTick, acSharedCrc, acClientCrc, acServerInputCrc, acClientInputCrc, rUpdate.statusChanges.size(), rFrameInput.statusChanges.size());

		rWork.iDesyncTick = iTick;
		rWork.desyncExpectedCrc = rUpdate.sharedCrc;
		rWork.desyncActualCrc = clientCrc;
		rWork.pDesyncClientFrame = CloneFrameViaSerialization(rCurrentFrame);
		return false;
	}

	// Validate input CRC
	common::crc_t clientInputCrc = rFrameInput.ServerInputCrc();
	if (clientInputCrc != rUpdate.inputCrc)
	{
		char acServerInputCrc[20] {}, acClientInputCrc[20] {};
		common::ToHex(std::span<char, 20>(acServerInputCrc), rUpdate.inputCrc);
		common::ToHex(std::span<char, 20>(acClientInputCrc), clientInputCrc);
		LOG(kNetwork, kVerbose, "ReconcileValidateCrcCoord Input desync Coord: ({},{}) Tick: {} ServerInputCrc: {} ClientInputCrc: {} ServerStatusChanges: {} ClientStatusChanges: {}", rWork.coord.x, rWork.coord.y, iTick, acServerInputCrc, acClientInputCrc, rUpdate.statusChanges.size(), rFrameInput.statusChanges.size());

		rWork.iDesyncTick = iTick;
		rWork.desyncExpectedCrc = rUpdate.inputCrc;
		rWork.desyncActualCrc = clientInputCrc;
		rWork.pDesyncClientFrame = CloneFrameViaSerialization(rCurrentFrame);
		return false;
	}

	// Record CRC-validated index
	rWork.iLastValidatedIndex = rWork.iReplayStackCount - 1;
	rWork.iNewConfirmedTick = iTick;
	rWork.iHighWaterValidatedTick = std::max(rWork.iHighWaterValidatedTick, iTick);

	return true;
}

static void ReconcileReplayCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork, int64_t iReplayStart, int64_t iRollbackOffset, int64_t iMaxConsecutive, float& rfTime)
{
	int64_t iAvailable = iMaxConsecutive - (iReplayStart - 1);
	constexpr int64_t kiLowJitterThresholdUs = 2000;
	constexpr int64_t kiHighJitterThresholdUs = 8000;
	int64_t iJitterUs = rReconcileContext.iJitterUs;
	int64_t iMaxReplay;
	if (iJitterUs <= kiLowJitterThresholdUs)
	{
		iMaxReplay = iAvailable;
	}
	else if (iJitterUs >= kiHighJitterThresholdUs)
	{
		iMaxReplay = std::max(iAvailable / 4, 1LL);
	}
	else
	{
		iMaxReplay = std::max(iAvailable / 2, 1LL);
	}
	// Gap-aware override: when backlog is large, allow more replay to prevent cascading failure
	int64_t iGap = rReconcileContext.iTargetTick - rWork.iConfirmedTick;
	static constexpr int64_t kiGapOverrideThreshold = engine::kiNetworkBufferSize / 2;
	if (iGap >= kiGapOverrideThreshold)
	{
		static constexpr int64_t kiRingBudget = engine::kiNetworkBufferSize * 3 / 4;
		iMaxReplay = std::max(iMaxReplay, std::min(iAvailable, kiRingBudget));
	}
	int64_t iReplayCount = 0;

	for (int64_t iTick = iReplayStart; iTick <= iMaxConsecutive; ++iTick)
	{
		if (iReplayCount >= iMaxReplay)
		{
			break;
		}

		auto updateIt = rWork.serverUpdates.find(iTick);
		if (updateIt == rWork.serverUpdates.end())
		{
			break;
		}

		rfTime += kfDeltaTime;

		FrameInput frameInput;
		frameInput.statusChanges = updateIt->second.statusChanges;

		if (!ReconcileRunTickCoord(rReconcileContext, rWork, iTick, rfTime, frameInput))
		{
			break;
		}

		// Inject pending full state at matching tick
		if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick == iTick)
		{
			ReconcileInjectPendingFullState(rWork);
			LOG(kNetwork, kVerbose, "ReconcileReplayCoord Injected pending full state Coord: ({},{}) Tick: {}", rWork.coord.x, rWork.coord.y, iTick);
		}

		// CRC validation
		if (!ReconcileValidateCrcCoord(rReconcileContext, rWork, iTick, updateIt->second, frameInput))
		{
			return;
		}

		// Profiling
		bool bHadStatusChanges = !updateIt->second.statusChanges.empty();

		// Consume server update
		rWork.serverUpdates.erase(updateIt);

		if (bHadStatusChanges)
		{
			++rWork.profiling.iStatusChangeReplayTicks;
		}
		else
		{
			++rWork.profiling.iKnockOnReplayTicks;
		}

		++iReplayCount;
	}

	// Record physical ring index of new confirmed frame
	if (rWork.iLastValidatedIndex >= 0)
	{
		if (rWork.iLastValidatedIndex == 0)
		{
			rWork.iNewConfirmedOffset = SnapshotIndex(rWork.iSnapshotHead, iRollbackOffset);
		}
		else
		{
			rWork.iNewConfirmedOffset = SnapshotIndex(rWork.iReplayWriteHead, rWork.iLastValidatedIndex - 1);
		}
	}
}

static void ReconcileCatchUpCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork, int64_t iTargetTick, float& rfTime, ReconcileProfiling& rProfiling)
{
	int64_t iStartWriteCount = rWork.iReplayWriteCount;
	int64_t iCurrentTick = rWork.replayStack[rWork.iReplayStackCount - 1]->interpolate.iTick;

	int64_t iBudget = engine::kiNetworkBufferSize - rWork.iReplayWriteCount;
	int64_t iCappedTarget = std::min(iTargetTick, iCurrentTick + iBudget);

	while (iCurrentTick < iCappedTarget)
	{
		++iCurrentTick;
		rfTime += kfDeltaTime;

		FrameInput emptyInput;
		if (!ReconcileRunTickCoord(rReconcileContext, rWork, iCurrentTick, rfTime, emptyInput))
		{
			break;
		}
		++rProfiling.iAssumedFrameTicks;
	}

	// Clear recalculated flag on catch-up frames (replay frames keep it for rendering)
	for (int64_t i = iStartWriteCount; i < rWork.iReplayWriteCount; ++i)
	{
		int64_t iSlot = SnapshotIndex(rWork.iReplayWriteHead, i);
		rWork.snapshots[iSlot]->interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
	}
}

void ReconcileCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork)
{
	// Aggressive CRC walk: finds the highest matching ring frame across all server updates
	// in range, advances iConfirmedTick/iConfirmedOffset to it, and reports the lowest
	// unresolved mismatch (if any) past the new confirmed point.
	CrcFastPathCoordResult fastPathResult = CrcFastPathProcessCoord(rWork, rReconcileContext.iTargetTick, rWork.profiling);
	if (fastPathResult.bHandled)
	{
		++rWork.profiling.iCrcFastPathEvents;
		return;
	}

	// The walk may have set iNewConfirmedTick/iNewConfirmedOffset via CrcApplyMatchResult —
	// preserve those as the floor result. If full replay validates further, ReconcileValidateCrcCoord
	// and ReconcileReplayCoord will overwrite them. iOutputCount must be recomputed from scratch
	// because walk's count included old speculative frames that replay will overwrite.
	rWork.bCrcFastPath = false;
	rWork.iOutputCount = 0;

	rWork.bFullReplay = true;

	// Determine rollback base: prefer the shrunk target (one tick before the lowest unresolved
	// mismatch), using the speculative ring frame at that logical offset as the starting state.
	// Fall back to iConfirmedTick if the shrunk target is unavailable or the walk didn't find
	// a mismatch (e.g., pending full state or gap-only path).
	int64_t iRollbackTick = rWork.iConfirmedTick;
	int64_t iRollbackOffset = rWork.iConfirmedOffset;
	bool bShrunkRollback = false;
	if (fastPathResult.iLowestUnresolvedMismatch > rWork.iConfirmedTick + 1 && !rWork.pendingFullState.has_value())
	{
		int64_t iShrunkTick = fastPathResult.iLowestUnresolvedMismatch - 1;
		int64_t iShrunkIndex = -1;
		for (int64_t i = 0; i < rWork.iSnapshotCount; ++i)
		{
			int64_t iPhysical = SnapshotIndex(rWork.iSnapshotHead, i);
			if (rWork.snapshots[iPhysical] != nullptr && rWork.snapshots[iPhysical]->interpolate.iTick == iShrunkTick)
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
		}
	}

	LOG(kNetwork, kVerbose, "ReconcileCoord Full replay Coord: ({},{}) Confirmed: {} RollbackTick: {} Shrunk: {} Target: {}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick, iRollbackTick, bShrunkRollback, rReconcileContext.iTargetTick);

	ReconcileRollbackCoord(rWork, iRollbackOffset);
	float fTime = rWork.replayStack[0]->interpolate.fCurrentTime;

	// Inject pending full state at confirmed frame (stale states rejected at receive time).
	// Only applies to the full-rollback path — shrunk rollback disables this branch above.
	if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick == rWork.iConfirmedTick)
	{
		ReconcileInjectPendingFullState(rWork);
		fTime = rWork.replayStack[0]->interpolate.fCurrentTime;
		LOG(kNetwork, kVerbose, "ReconcileCoord Injected pending full state Coord: ({},{}) AtTick: {}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick);
	}
	else if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick < rWork.iConfirmedTick)
	{
		LOG(kNetwork, kVerbose, "ReconcileCoord Discarded stale pending full state Coord: ({},{}) FullStateTick: {} ConfirmedTick: {}", rWork.coord.x, rWork.coord.y, rWork.pendingFullState->iTick, rWork.iConfirmedTick);
		rWork.pendingFullState.reset();
	}

	int64_t iReplayStart = iRollbackTick + 1;
	int64_t iMaxConsecutive = std::min(ReconcileFindReplayRangeCoord(rWork, iReplayStart), rReconcileContext.iTargetTick);

	ReconcileReplayCoord(rReconcileContext, rWork, iReplayStart, iRollbackOffset, iMaxConsecutive, fTime);

	// Two-tier rollback fallback: if shrunk rollback desynced at the very first replay tick,
	// the speculative starting state was bad. Clear the desync, reset replay state, and retry
	// with a full rollback to iConfirmedTick.
	if (rWork.iDesyncTick >= 0 && bShrunkRollback && rWork.iDesyncTick == iReplayStart)
	{
		LOG(kNetwork, kVerbose, "ReconcileCoord Shrunk rollback failed Coord: ({},{}) DesyncTick: {} — falling back to full rollback", rWork.coord.x, rWork.coord.y, rWork.iDesyncTick);
		rWork.iDesyncTick = -1;
		rWork.desyncExpectedCrc = 0;
		rWork.desyncActualCrc = 0;
		rWork.pDesyncClientFrame.reset();
		rWork.iLastValidatedIndex = -1;

		iRollbackTick = rWork.iConfirmedTick;
		iRollbackOffset = rWork.iConfirmedOffset;
		bShrunkRollback = false;

		ReconcileRollbackCoord(rWork, iRollbackOffset);
		fTime = rWork.replayStack[0]->interpolate.fCurrentTime;

		if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick == rWork.iConfirmedTick)
		{
			ReconcileInjectPendingFullState(rWork);
			fTime = rWork.replayStack[0]->interpolate.fCurrentTime;
		}

		iReplayStart = iRollbackTick + 1;
		iMaxConsecutive = std::min(ReconcileFindReplayRangeCoord(rWork, iReplayStart), rReconcileContext.iTargetTick);
		ReconcileReplayCoord(rReconcileContext, rWork, iReplayStart, iRollbackOffset, iMaxConsecutive, fTime);
	}

	if (rWork.iDesyncTick >= 0)
	{
		return;
	}

	ReconcileCatchUpCoord(rReconcileContext, rWork, rReconcileContext.iTargetTick, fTime, rWork.profiling);

	// Compute output layout: confirmed frame + remaining replay/catch-up frames
	if (rWork.iLastValidatedIndex > 0)
	{
		rWork.iOutputCount = rWork.iReplayWriteCount - (rWork.iLastValidatedIndex - 1);
	}
	else if (rWork.iLastValidatedIndex == 0)
	{
		rWork.iOutputCount = rWork.iReplayWriteCount + 1;
	}
	else if (rWork.iNewConfirmedTick >= 0)
	{
		// Walk advanced iConfirmedTick but full replay didn't validate anything further.
		// Preserve walk's confirmed frame as the base and include new catch-up frames.
		rWork.iOutputCount = rWork.iReplayWriteCount + 1;
	}

	rWork.iOutputCount = std::min(rWork.iOutputCount, static_cast<int64_t>(engine::kiNetworkBufferSize));
	ASSERT(rWork.iOutputCount >= 0 && rWork.iOutputCount <= engine::kiNetworkBufferSize);

	rWork.iTickCounter = rReconcileContext.iTargetTick;
	ASSERT(rWork.replayStack[rWork.iReplayStackCount - 1]->interpolate.iTick <= rReconcileContext.iTargetTick);
	rWork.fCurrentTime = fTime;
}

#endif // BT_CLIENT

} // namespace game
