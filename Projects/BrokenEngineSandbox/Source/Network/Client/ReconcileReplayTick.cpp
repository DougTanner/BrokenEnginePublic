#include "Network/Client/ReconcileReplay.h"

#include "Game.h"
#include "Frame/FrameTick.h"
#include "Frame/StatusChange.h"
#include "Network/Client/ClientReconciler.h"
#include "SpawnTransfer.h"

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

void ReconcileRollbackCoord(CoordWork& rWork, int64_t iRollbackOffset)
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

int64_t ReconcileFindReplayRangeCoord(CoordWork& rWork, int64_t iReplayStart)
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

// Emit the per-tick spawn-transfer summary at kVerbose. Two formats: with-PlayerIds (when any
// transferred player IDs are captured) or counts-only (when only non-player transfers occurred).
static void LogTransferSummary(const CoordWork& rWork, int64_t iTick, int64_t iTransferPlayerCount, int64_t iTransferBlasterCount, int64_t iTransferSpaceshipCount, int64_t iTransferMissileCount, const engine::global_id_t* pTransferPlayerIds, int64_t iTransferPlayerIdCount)
{
	// NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) — read only by the kVerbose LOGs below, which compile out at default log levels
	int64_t iTransferTotal = iTransferPlayerCount + iTransferBlasterCount + iTransferSpaceshipCount + iTransferMissileCount;
	if (iTransferPlayerCount > 0)
	{
		char acPlayerIds[192] {};
		size_t iPos = 0;
		for (int64_t i = 0; i < iTransferPlayerIdCount; ++i)
		{
			constexpr size_t kiReserve = 24;
			if (iPos + kiReserve > sizeof(acPlayerIds))
			{
				break;
			}
			if (i > 0)
			{
				acPlayerIds[iPos++] = ',';
				acPlayerIds[iPos++] = ' ';
			}
			int iWritten = std::snprintf(acPlayerIds + iPos, sizeof(acPlayerIds) - iPos, "%lld", pTransferPlayerIds[i].iValue);
			if (iWritten <= 0)
			{
				break;
			}
			iPos += static_cast<size_t>(iWritten);
		}
		LOG(kNetwork, kVerbose, "ReconcileRunTickCoord SpawnTransfers Coord: ({},{}) ForTick: {} TransferCount: {} PlayerCount: {} BlasterCount: {} SpaceshipCount: {} MissileCount: {} PlayerIds: [{}]", rWork.coord.x, rWork.coord.y, iTick, iTransferTotal, iTransferPlayerCount, iTransferBlasterCount, iTransferSpaceshipCount, iTransferMissileCount, acPlayerIds);
	}
	else
	{
		LOG(kNetwork, kVerbose, "ReconcileRunTickCoord SpawnTransfers Coord: ({},{}) ForTick: {} TransferCount: {} PlayerCount: {} BlasterCount: {} SpaceshipCount: {} MissileCount: {}", rWork.coord.x, rWork.coord.y, iTick, iTransferTotal, iTransferPlayerCount, iTransferBlasterCount, iTransferSpaceshipCount, iTransferMissileCount);
	}
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
				case StatusChangeType::kTransferBlaster:
					++iTransferBlasterCount;
					break;
				case StatusChangeType::kTransferSpaceship:
					++iTransferSpaceshipCount;
					break;
				case StatusChangeType::kTransferMissile:
					++iTransferMissileCount;
					break;
				default:
					break;
			}
		}
	}
	if (bHadTransfers && !bIsReplay && iTick > rWork.pFrames->iLastSpawnTransferLogTick)
	{
		rWork.pFrames->iLastSpawnTransferLogTick = iTick;
		LogTransferSummary(rWork, iTick, iTransferPlayerCount, iTransferBlasterCount, iTransferSpaceshipCount, iTransferMissileCount, transferPlayerIds, std::min(iTransferPlayerCount, int64_t {8}));
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
		if (!(rScratch.flags & ReconcileScratchFlags::kSuppressRepeatLogs))
		{
			char acSharedCrc[20] {}, acClientCrc[20] {};
			common::ToHex(std::span<char, 20>(acSharedCrc), rUpdate.sharedCrc);
			common::ToHex(std::span<char, 20>(acClientCrc), clientCrc);
			LOG(kNetwork, kDebug, "ReconcileValidateCrcCoord Replay CRC mismatch; reconciliation outcome pending Coord: ({},{}) ForTick: {} ServerCrc: {} ClientCrc: {} ServerStatusChanges: {} ClientStatusChanges: {}", rWork.coord.x, rWork.coord.y, iTick, acSharedCrc, acClientCrc, rUpdate.statusChanges.size(), rFrameInput.statusChanges.size());
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

void ReconcileReplayCoord(CoordWork& rWork, int64_t iReplayStart, int64_t iRollbackOffset, int64_t iMaxConsecutive, float& rfTime)
{
	engine::CoordFrames& rFrames = *rWork.pFrames;
	CoordScratch& rScratch = rWork.scratch;

	for (int64_t iTick = iReplayStart; iTick <= iMaxConsecutive; ++iTick)
	{
		auto updateIt = rFrames.serverUpdates.find(iTick);
		if (updateIt == rFrames.serverUpdates.end())
		{
			break;
		}

		rfTime += kfDeltaTime;

		if (iTick <= rScratch.iPreReconcileTailTick)
		{
			rScratch.flags.Set(ReconcileScratchFlags::kReSimOccurred);
		}

		FrameInput frameInput;
		frameInput.statusChanges = updateIt->second.statusChanges;

		if (!ReconcileRunTickCoord(rWork, iTick, rfTime, frameInput, true))
		{
			break;
		}

		// The replay range is target-capped, so a matching pending full state is necessarily due.
		if (rFrames.pendingFullState.has_value() && rFrames.pendingFullState->iTick == iTick)
		{
			ReconcileInjectPendingFullState(rWork);
			rfTime = rScratch.replayStack.at(0)->interpolate.fCurrentTime;
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
	}

	// Record the replay output layout with the confirmed frame at its head.
	if (rScratch.iLastValidatedIndex >= 0)
	{
		if (rScratch.iLastValidatedIndex == 0)
		{
			rScratch.outputLayout.iHead = SnapshotIndex(rFrames.iSnapshotHead, iRollbackOffset);
		}
		else
		{
			rScratch.outputLayout.iHead = SnapshotIndex(rScratch.iReplayWriteHead, rScratch.iLastValidatedIndex - 1);
		}
		rScratch.outputLayout.iConfirmedInner = 0;
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

void ReconcileCatchUpCoord(CoordWork& rWork, int64_t iTargetTick, float& rfTime)
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
void ReconcileFastPathCatchUp(CoordWork& rWork, int64_t iTargetTick)
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

#endif // BT_CLIENT

} // namespace game
