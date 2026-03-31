#include "Game.h"

#include "Network/ReconcileReplay.h"

#include "Frame/FrameTick.h"
#include "Network/ClientReconciler.h"
#include "Frame/Collections/Players/Players.h"

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
	rWork.pendingFullState.reset();
}

// --- Per-coord reconciliation functions ---

static void ReconcileRollbackCoord(CoordReconcileWork& rWork)
{
	ASSERT(rWork.iConfirmedOffset >= 0);
	int64_t iConfirmedPhysical = SnapshotIndex(rWork.iSnapshotHead, rWork.iConfirmedOffset);
	rWork.replayStack.clear();
	rWork.replayStack.push_back(rWork.snapshots[iConfirmedPhysical].get());
	rWork.iReplayStackCount = 1;
	rWork.iReplayWriteHead = SnapshotIndex(rWork.iSnapshotHead, rWork.iConfirmedOffset + 1);
	rWork.iReplayWriteCount = 0;
}

static int64_t ReconcileFindReplayRangeCoord(CoordReconcileWork& rWork)
{
	int64_t iReplayStart = rWork.iConfirmedTick + 1;
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

static bool ReconcileRunTickCoord(CoordReconcileWork& rWork, int64_t iTick, float fTime, FrameInput& rFrameInput)
{
	if (rWork.iReplayWriteCount >= engine::kiNetworkBufferSize)
	{
		Log(kLogNetwork, "ReconcileRunTickCoord Ring buffer full WriteCount: {} Tick: {}", rWork.iReplayWriteCount, iTick);
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
	};
	RunFrameTick(ref, iTick, fTime);

	// Apply transfer StatusChanges (runs after Destroy/Spawn to match server ordering)
	bool bHadTransfers = false;
	for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
	{
		if (IsTransferType(rStatusChange.eType))
		{
			SpawnTransfer(*pNext, rStatusChange.eType, rStatusChange.data, pNext->postRender.playerAlignment);
			bHadTransfers = true;

			if (rStatusChange.eType == StatusChangeType::kTransferPlayer)
			{
				// DT TEMP
				Log(kLogNetwork, "ReconcileRunTickCoord SpawnTransfer TransferPlayer Coord: ({},{}) Tick: {} PlayerCount: {}", rWork.coord.x, rWork.coord.y, iTick, pNext->postRender.pPlayers->iCount);
			}
		}
	}
	std::erase_if(rFrameInput.statusChanges, [](const StatusChange& rStatusChange)
	{
		return IsTransferType(rStatusChange.eType);
	});

	if (bHadTransfers)
	{
		auto [crc, sharedCrc] = pNext->Crcs();
		pNext->postRender.crc = crc;
		pNext->postRender.sharedCrc = sharedCrc;
	}

	// Advance replay stack
	rWork.replayStack.push_back(pNext);
	++rWork.iReplayStackCount;
	++rWork.iReplayWriteCount;

	return true;
}

static bool ReconcileValidateCrcCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork, int64_t iTick, const engine::CoordFrames::CoordServerUpdate& rUpdate, const FrameInput& rFrameInput)
{
	Frame& rCurrentFrame = *rWork.replayStack[rWork.iReplayStackCount - 1];
	rCurrentFrame.interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
	common::crc_t clientCrc = rCurrentFrame.postRender.sharedCrc;

	if (clientCrc != rUpdate.sharedCrc)
	{
		char acSharedCrc[20] {}, acClientCrc[20] {}, acPrevCrc[20] {};
		common::ToHex(std::span<char, 20>(acSharedCrc), rUpdate.sharedCrc);
		common::ToHex(std::span<char, 20>(acClientCrc), clientCrc);
		common::ToHex(std::span<char, 20>(acPrevCrc), rCurrentFrame.postRender.previousCrc);
		Log(kLogNetwork, "ReconcileValidateCrcCoord Desync Coord: ({},{}) Frame: {} SharedCrc: {} ClientCrc: {} PrevCrc: {}", rWork.coord.x, rWork.coord.y, iTick, acSharedCrc, acClientCrc, acPrevCrc);
		{
			ScopedLogIndent scopedCrcIndent;
			char acServerInputCrc[20] {}, acClientInputCrc[20] {};
			common::ToHex(std::span<char, 20>(acServerInputCrc), rUpdate.inputCrc);
			common::ToHex(std::span<char, 20>(acClientInputCrc), rFrameInput.ServerInputCrc());
			Log(kLogNetwork, "ServerInputCrc: {} ClientInputCrc: {}", acServerInputCrc, acClientInputCrc);
			LogStatusChangeList("Server StatusChanges", rUpdate.statusChanges);
			LogStatusChangeList("Client StatusChanges", rFrameInput.statusChanges);
		}

		if (rWork.coord != rReconcileContext.confirmedClientState.clientGridCoord)
		{
			Log(kLogNetwork, "ReconcileValidateCrcCoord Neighbor CRC mismatch (non-fatal) Coord: ({},{}) Frame: {}", rWork.coord.x, rWork.coord.y, iTick);
			rWork.iLastValidatedIndex = rWork.iReplayStackCount - 1;
			rWork.iNewConfirmedTick = iTick;
			return true;
		}

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
		Log(kLogNetwork, "ReconcileValidateCrcCoord Input desync Coord: ({},{}) Frame: {} ServerInputCrc: {} ClientInputCrc: {}", rWork.coord.x, rWork.coord.y, iTick, acServerInputCrc, acClientInputCrc);
		{
			ScopedLogIndent scopedInputIndent;
			LogStatusChangeList("Server StatusChanges", rUpdate.statusChanges);
			LogStatusChangeList("Client StatusChanges", rFrameInput.statusChanges);
		}

		if (rWork.coord != rReconcileContext.confirmedClientState.clientGridCoord)
		{
			Log(kLogNetwork, "ReconcileValidateCrcCoord Neighbor input CRC mismatch (non-fatal) Coord: ({},{}) Frame: {}", rWork.coord.x, rWork.coord.y, iTick);
			rWork.iLastValidatedIndex = rWork.iReplayStackCount - 1;
			rWork.iNewConfirmedTick = iTick;
			return true;
		}

		rWork.iDesyncTick = iTick;
		rWork.desyncExpectedCrc = rUpdate.inputCrc;
		rWork.desyncActualCrc = clientInputCrc;
		rWork.pDesyncClientFrame = CloneFrameViaSerialization(rCurrentFrame);
		return false;
	}

	// Record CRC-validated index
	rWork.iLastValidatedIndex = rWork.iReplayStackCount - 1;
	rWork.iNewConfirmedTick = iTick;

	return true;
}

static void ReconcileReplayCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork, int64_t iMaxConsecutive, float& rfTime)
{
	int64_t iReplayStart = rWork.iConfirmedTick + 1;
	int64_t iAvailable = iMaxConsecutive - rWork.iConfirmedTick + 1;
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

		if (!ReconcileRunTickCoord(rWork, iTick, rfTime, frameInput))
		{
			break;
		}

		// Inject pending full state at matching tick
		if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick == iTick)
		{
			ReconcileInjectPendingFullState(rWork);
			Log(kLogNetwork, "ReconcileReplayCoord Injected pending full state Coord: ({},{}) Tick: {}", rWork.coord.x, rWork.coord.y, iTick);
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
			rWork.iNewConfirmedOffset = SnapshotIndex(rWork.iSnapshotHead, rWork.iConfirmedOffset);
		}
		else
		{
			rWork.iNewConfirmedOffset = SnapshotIndex(rWork.iReplayWriteHead, rWork.iLastValidatedIndex - 1);
		}
	}
}

static void ReconcileCatchUpCoord(CoordReconcileWork& rWork, int64_t iTargetTick, float& rfTime, ReconcileProfiling& rProfiling)
{
	int64_t iStartWriteCount = rWork.iReplayWriteCount;
	int64_t iCurrentTick = rWork.replayStack[rWork.iReplayStackCount - 1]->interpolate.iTick;

	while (iCurrentTick < iTargetTick)
	{
		++iCurrentTick;
		rfTime += kfDeltaTime;

		FrameInput emptyInput;
		if (!ReconcileRunTickCoord(rWork, iCurrentTick, rfTime, emptyInput))
		{
			break;
		}
		++rProfiling.iAssumedFrameTicks;
	}

	ASSERT(iCurrentTick <= iTargetTick);

	// Clear recalculated flag on catch-up frames (replay frames keep it for rendering)
	for (int64_t i = iStartWriteCount; i < rWork.iReplayWriteCount; ++i)
	{
		int64_t iSlot = SnapshotIndex(rWork.iReplayWriteHead, i);
		rWork.snapshots[iSlot]->interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
	}
}

void ReconcileCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork)
{
	// Try CRC fast path first
	CrcFastPathCoordResult fastPathResult = CrcFastPathProcessCoord(rWork, rReconcileContext.iTargetTick, rWork.profiling);
	if (fastPathResult.bHandled)
	{
		++rWork.profiling.iCrcFastPathEvents;
		return;
	}

	// Partial CRC match may have set fast-path output fields — reset them for full replay
	rWork.bCrcFastPath = false;
	rWork.iNewConfirmedTick = -1;
	rWork.iNewConfirmedOffset = -1;
	rWork.iOutputCount = 0;

	rWork.bFullReplay = true;

	Log(kLogNetwork, "ReconcileCoord Full replay Coord: ({},{}) Confirmed: {} Target: {}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick, rReconcileContext.iTargetTick);

	ReconcileRollbackCoord(rWork);
	float fTime = rWork.replayStack[0]->interpolate.fCurrentTime;

	// Inject pending full state at confirmed frame (stale states rejected at receive time)
	if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick == rWork.iConfirmedTick)
	{
		ReconcileInjectPendingFullState(rWork);
		fTime = rWork.replayStack[0]->interpolate.fCurrentTime;
		Log(kLogNetwork, "ReconcileCoord Injected pending full state Coord: ({},{}) AtTick: {}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick);
	}
	else if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick < rWork.iConfirmedTick)
	{
		Log(kLogNetwork, "ReconcileCoord Discarded stale pending full state Coord: ({},{}) FullStateTick: {} ConfirmedTick: {}", rWork.coord.x, rWork.coord.y, rWork.pendingFullState->iTick, rWork.iConfirmedTick);
		rWork.pendingFullState.reset();
	}

	int64_t iMaxConsecutive = std::min(ReconcileFindReplayRangeCoord(rWork), rReconcileContext.iTargetTick);

	Log(kLogNetwork, "ReconcileCoord ReplayRange Coord: ({},{}) MaxConsecutive: {} Size: {}", rWork.coord.x, rWork.coord.y, iMaxConsecutive, iMaxConsecutive - rWork.iConfirmedTick);

	ReconcileReplayCoord(rReconcileContext, rWork, iMaxConsecutive, fTime);
	if (rWork.iDesyncTick >= 0)
	{
		return;
	}

	ReconcileCatchUpCoord(rWork, rReconcileContext.iTargetTick, fTime, rWork.profiling);

	// Compute output layout: confirmed frame + remaining replay/catch-up frames
	if (rWork.iLastValidatedIndex > 0)
	{
		rWork.iOutputCount = rWork.iReplayWriteCount - (rWork.iLastValidatedIndex - 1);
	}
	else if (rWork.iLastValidatedIndex == 0)
	{
		rWork.iOutputCount = rWork.iReplayWriteCount + 1;
	}

	rWork.iOutputCount = std::min(rWork.iOutputCount, static_cast<int64_t>(engine::kiNetworkBufferSize));
	ASSERT(rWork.iOutputCount >= 0 && rWork.iOutputCount <= engine::kiNetworkBufferSize);

	// Set per-coord counters from this coord's final state
	rWork.iTickCounter = rReconcileContext.iTargetTick;
	ASSERT(rWork.replayStack[rWork.iReplayStackCount - 1]->interpolate.iTick <= rReconcileContext.iTargetTick);
	rWork.fCurrentTime = fTime;
}

#endif // BT_CLIENT

} // namespace game
