#include "Game.h"

#include "Network/ReconcileReplay.h"

#include "Frame/FrameTick.h"
#include "Frame/Collections/Players/Players.h"
#include "Network/ClientReconciler.h"

namespace game
{

#if defined(BT_CLIENT)

static void LogStatusChangeDetail(const StatusChange& rStatusChange)
{
	Log(kLogNetwork, "Type: {}", StatusChangeTypeName(rStatusChange.eType));
	ScopedLogIndent scopedDetail;
	const TransferData& rData = rStatusChange.data;
	Log(kLogNetwork, "Position: {} Direction: {} Velocity: {}", rData.vecPosition, rData.vecDirection, rData.vecVelocity);
	char acAlignment[20] {};
	common::ToHex(std::span<char, 20>(acAlignment), rData.alignment.uiValue);
	Log(kLogNetwork, "Alignment: {} Health: {} Shield: {} TypeIndex: {}", acAlignment, rData.fHealth, rData.fShield, rData.uiTypeIndex);
	Log(kLogNetwork, "WindTrailIntensity: {} WindTrailWidth: {} WindTrailLengthMultiplier: {} Acceleration: {}", rData.fWindTrailIntensity, rData.fWindTrailWidth, rData.fWindTrailLengthMultiplier, rData.fAcceleration);
	Log(kLogNetwork, "NextBlasterFireTime: {} NextSecondarySpawnTime: {} ShieldCooldown: {} ShieldDownSoundCooldown: {}", rData.fNextBlasterFireTime, rData.fNextSecondarySpawnTime, rData.fShieldCooldown, rData.fShieldDownSoundCooldown);
	Log(kLogNetwork, "AnimationTime: {} ShieldRotation: {} ShieldShrink: {} PlayerFlags: {}", rData.fAnimationTime, rData.fShieldRotation, rData.fShieldShrink, rData.uiPlayerFlags);
	Log(kLogNetwork, "NextBlasterSpawnTime: {}", rData.fNextBlasterSpawnTime);
	Log(kLogNetwork, "DeltaRotationDelay: {} Time: {} ExhaustDelay: {} NextJitter: {}", rData.fDeltaRotationDelay, rData.fTime, rData.fExhaustDelay, rData.fNextJitter);
}

static void LogStatusChangeList(std::string_view label, std::span<const StatusChange> statusChanges)
{
	Log(kLogNetwork, "{} Count: {}", label, statusChanges.size());
	ScopedLogIndent scopedList;
	for (size_t i = 0; i < statusChanges.size(); ++i)
	{
		Log(kLogNetwork, "[{}]", i);
		ScopedLogIndent scopedEntry;
		LogStatusChangeDetail(statusChanges[i]);
	}
}

static void ReconcileTrackHumanMigration(ReconcileContext& rReconcileContext)
{
	const int64_t iActiveCount = static_cast<int64_t>(rReconcileContext.activeCoords.size());

	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		const engine::GridCoord& rCoord = rReconcileContext.activeCoords.at(static_cast<size_t>(j));
		CoordReconcileWork* pWork = rReconcileContext.FindCoordWork(rCoord);
		if (pWork == nullptr) { continue; }
		const Frame& rNext = *pWork->replayWorkspace[pWork->iReplayWorkspaceUsed];
		for (const TransferRequest& rRequest : rNext.postRender.transferRequests)
		{
			if (rRequest.eType == StatusChangeType::kTransferPlayer &&
				rReconcileContext.humanPlayerId.IsValid() &&
				rRequest.iEntityId == rReconcileContext.humanPlayerId.ToUuid().Value())
			{
				engine::GridCoord destination {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};
				rReconcileContext.humanGridCoord = destination;

				// Find human's new ID by position-matching in destination frame (server already spawned there)
				CoordReconcileWork* pDestWork = rReconcileContext.FindCoordWork(destination);
				if (pDestWork != nullptr)
				{
					CoordReconcileWork& rDestWork = *pDestWork;
					if (rDestWork.iReplayStackCount > 0)
					{
						const Frame& rDestinationFrame = *rDestWork.replayWorkspace[rDestWork.iReplayWorkspaceUsed];
						bool bFound = false;
						for (int64_t i = 0; i < rDestinationFrame.postRender.pPlayers->iCount; ++i)
						{
							if (XMVector4Equal(rDestinationFrame.interpolate.pPlayers->pVecPositions[i], rRequest.data.vecPosition))
							{
								rReconcileContext.humanPlayerId = rDestinationFrame.postRender.pPlayers->puiIds[i];
								bFound = true;
								break;
							}
						}
						if (!bFound && rDestinationFrame.postRender.pPlayers->iCount > 0)
						{
							rReconcileContext.humanPlayerId = rDestinationFrame.postRender.pPlayers->puiIds[rDestinationFrame.postRender.pPlayers->iCount - 1];
						}
					}
				}
				rReconcileContext.fPreviousHumanArmor = rRequest.data.fHealth;
			}
		}
	}
}

static void ReconcileEnsureWorkspaceFrames(ReconcileContext& rReconcileContext)
{
	const int64_t iActiveCount = static_cast<int64_t>(rReconcileContext.activeCoords.size());
	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		const engine::GridCoord& rCoord = rReconcileContext.activeCoords.at(static_cast<size_t>(j));
		CoordReconcileWork* pWork = rReconcileContext.FindCoordWork(rCoord);
		if (pWork == nullptr) { continue; }
		int64_t iNextWorkspace = pWork->iReplayWorkspaceUsed;
		if (iNextWorkspace >= static_cast<int64_t>(pWork->replayWorkspace.size()))
		{
			pWork->replayWorkspace.resize(static_cast<size_t>(iNextWorkspace + 1));
		}
		if (pWork->replayWorkspace[iNextWorkspace] == nullptr)
		{
			pWork->replayWorkspace[iNextWorkspace] = std::make_unique<Frame>();
		}
	}
}

static std::span<const ActiveFrameRef> ReconcileBuildActiveFrameRefs(ReconcileContext& rReconcileContext)
{
	const int64_t iActiveCount = static_cast<int64_t>(rReconcileContext.activeCoords.size());
	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		const engine::GridCoord& rCoord = rReconcileContext.activeCoords.at(static_cast<size_t>(j));
		CoordReconcileWork* pWork = rReconcileContext.FindCoordWork(rCoord);
		if (pWork == nullptr) { continue; }
		Frame* pCurrent = pWork->replayStack[pWork->iReplayStackCount - 1];
		Frame* pNext = pWork->replayWorkspace[pWork->iReplayWorkspaceUsed].get();
		common::gpThreadLocal->mWorkbuffer.PushBack<ActiveFrameRef>({
			.pNext = pNext,
			.pCurrent = pCurrent,
			.pFrameInput = &rReconcileContext.frameInputs.at(rCoord),
		});
	}
	return common::gpThreadLocal->mWorkbuffer.Span<ActiveFrameRef>();
}

static void ReconcileExecuteTick(ReconcileContext& rReconcileContext, std::span<const ActiveFrameRef> activeFrameRefs)
{
	const int64_t iRefCount = static_cast<int64_t>(activeFrameRefs.size());

	// Set kRecalculated before running tick so non-deterministic side effects are suppressed
	for (int64_t j = 0; j < iRefCount; ++j)
	{
		activeFrameRefs[j].pNext->interpolate.frameFlags.Set(engine::FrameFlags::kRecalculated);
	}

	// Run tick sequentially (reconcile runs on single kThreadReconcile worker)
	for (int64_t j = 0; j < iRefCount; ++j)
	{
		RunFrameTick(activeFrameRefs[j], rReconcileContext.iTickCounter, rReconcileContext.fCurrentTime);
	}

	// Apply server-provided transfer StatusChanges per-coord (no cross-coord coupling)
	// Runs after Destroy/Spawn to match server ordering (HarvestTransfers runs after Destroy/Spawn)
	for (int64_t j = 0; j < iRefCount; ++j)
	{
		Frame& rNext = *activeFrameRefs[j].pNext;
		FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;

		bool bHadTransfers = false;
		for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
		{
			if (IsTransferType(rStatusChange.eType))
			{
				SpawnTransfer(rNext, rStatusChange.eType, rStatusChange.data, rNext.postRender.playerAlignment);
				bHadTransfers = true;
			}
		}
		std::erase_if(rFrameInput.statusChanges, [](const StatusChange& rStatusChange)
		{
			return IsTransferType(rStatusChange.eType);
		});

		// Recompute CRCs after transfers modified the frame
		// (RunFrameTick computed CRCs before transfers were applied)
		if (bHadTransfers)
		{
			rNext.postRender.serverCrc = rNext.ServerCrc();
			rNext.postRender.crc = rNext.Crc();
		}
	}
}

static void ReconcileAdvanceStack(ReconcileContext& rReconcileContext)
{
	const int64_t iActiveCount = static_cast<int64_t>(rReconcileContext.activeCoords.size());
	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		const engine::GridCoord& rCoord = rReconcileContext.activeCoords.at(static_cast<size_t>(j));
		CoordReconcileWork* pWork = rReconcileContext.FindCoordWork(rCoord);
		if (pWork == nullptr) { continue; }
		pWork->replayStack.push_back(pWork->replayWorkspace[pWork->iReplayWorkspaceUsed].get());
		pWork->iReplayStackCount++;
		pWork->iReplayWorkspaceUsed++;
	}

	for (auto& [rCoord, rFrameInput] : rReconcileContext.frameInputs)
	{
		rFrameInput.statusChanges.clear();
	}
}

void ReconcileRunTick(ReconcileContext& rReconcileContext)
{
	ReconcileEnsureWorkspaceFrames(rReconcileContext);

	common::gpThreadLocal->mWorkbuffer.Push();
	std::span<const ActiveFrameRef> activeFrameRefs = ReconcileBuildActiveFrameRefs(rReconcileContext);
	ReconcileExecuteTick(rReconcileContext, activeFrameRefs);
	common::gpThreadLocal->mWorkbuffer.Pop();

	ReconcileTrackHumanMigration(rReconcileContext);
	ReconcileAdvanceStack(rReconcileContext);
}

static int64_t FindSnapshotIndex(std::unique_ptr<Frame> (&rSnapshots)[engine::kiTickRate], int64_t iHead, int64_t iCount, int64_t iTick)
{
	for (int64_t i = 0; i < iCount; ++i)
	{
		int64_t iPhysical = SnapshotIndex(iHead, i);
		if (rSnapshots[iPhysical] != nullptr && rSnapshots[iPhysical]->interpolate.iTick == iTick)
		{
			return i;
		}
	}
	return -1;
}

struct CrcValidateResult
{
	bool bMatch = true;
	int64_t iLastMatched = -1;
	int64_t iLastMatchedIndex = -1;
};

static CrcValidateResult CrcValidateLoop(CoordReconcileWork& rWork, int64_t iTargetTick)
{
	CrcValidateResult result;
	int64_t iExpected = rWork.iConfirmedTick + 1;
	auto it = rWork.serverUpdates.begin();

	while (it != rWork.serverUpdates.end() && it->first == iExpected && iExpected <= iTargetTick)
	{
		int64_t iIndex = FindSnapshotIndex(rWork.snapshots, rWork.iSnapshotHead, rWork.iSnapshotCount, iExpected);
		if (iIndex < 0)
		{
			break;
		}
		int64_t iPhysical = SnapshotIndex(rWork.iSnapshotHead, iIndex);
		if (rWork.snapshots[iPhysical]->postRender.serverCrc != it->second.serverCrc)
		{
			char acServerCrc[20] {}, acClientCrc[20] {}, acPrevCrc[20] {};
			common::ToHex(std::span<char, 20>(acServerCrc), it->second.serverCrc);
			common::ToHex(std::span<char, 20>(acClientCrc), rWork.snapshots[iPhysical]->postRender.serverCrc);
			common::ToHex(std::span<char, 20>(acPrevCrc), rWork.snapshots[iPhysical]->postRender.previousCrc);
			Log(kLogNetwork, "CrcValidateLoop Server CRC mismatch Coord: ({},{}) Tick: {} ServerCrc: {} ClientCrc: {} PrevCrc: {}", rWork.coord.x, rWork.coord.y, iExpected, acServerCrc, acClientCrc, acPrevCrc);
			{
				ScopedLogIndent scopedCrcIndent;
				char acServerInputCrc[20] {}, acClientInputCrc[20] {};
				common::ToHex(std::span<char, 20>(acServerInputCrc), it->second.inputCrc);
				common::ToHex(std::span<char, 20>(acClientInputCrc), rWork.snapshots[iPhysical]->postRender.previousInputCrc);
				Log(kLogNetwork, "ServerInputCrc: {} ClientInputCrc: {}", acServerInputCrc, acClientInputCrc);
				LogStatusChangeList("Server StatusChanges", it->second.statusChanges);
			}
			result.bMatch = false;
			break;
		}
		if (rWork.snapshots[iPhysical]->postRender.previousInputCrc != it->second.inputCrc)
		{
			char acServerInputCrc[20] {}, acClientInputCrc[20] {};
			common::ToHex(std::span<char, 20>(acServerInputCrc), it->second.inputCrc);
			common::ToHex(std::span<char, 20>(acClientInputCrc), rWork.snapshots[iPhysical]->postRender.previousInputCrc);
			Log(kLogNetwork, "CrcValidateLoop Input CRC mismatch Coord: ({},{}) Tick: {} ServerInputCrc: {} ClientInputCrc: {}", rWork.coord.x, rWork.coord.y, iExpected, acServerInputCrc, acClientInputCrc);
			{
				ScopedLogIndent scopedInputIndent;
				LogStatusChangeList("Server StatusChanges", it->second.statusChanges);
			}
			result.bMatch = false;
			break;
		}
		result.iLastMatched = iExpected;
		result.iLastMatchedIndex = iIndex;
		++iExpected;
		++it;
	}

	return result;
}

static void CrcApplyMatchResult(CoordReconcileWork& rWork, int64_t iLastMatched, int64_t iLastMatchedIndex, ReconcileContext::Profiling& rProfiling)
{
	rWork.bCrcFastPath = true;
	rWork.iNewConfirmedTick = iLastMatched;
	rProfiling.iCrcValidatedFrameTicks += iLastMatched - rWork.iConfirmedTick;

	// Record matched snapshot logical offset instead of moving Frame
	rWork.iNewConfirmedOffset = iLastMatchedIndex;

	// Keep snapshots beyond matched frame
	rWork.newSnapshots.clear();
	for (int64_t i = 0; i < rWork.iSnapshotCount; ++i)
	{
		int64_t iPhys = SnapshotIndex(rWork.iSnapshotHead, i);
		if (rWork.snapshots[iPhys] != nullptr && rWork.snapshots[iPhys]->interpolate.iTick > iLastMatched)
		{
			rWork.newSnapshots.push_back(std::move(rWork.snapshots[iPhys]));
		}
	}
}

struct CrcFastPathCoordResult
{
	bool bHandled = true;
	int64_t iMinConfirmedContrib = -1;
	int64_t iNewMinConfirmedContrib = -1;
};

static CrcFastPathCoordResult CrcFastPathProcessCoord(CoordReconcileWork& rWork, int64_t iTargetTick, ReconcileContext::Profiling& rProfiling)
{
	CrcFastPathCoordResult result;

	if (rWork.serverUpdates.empty() && !rWork.pendingFullState.has_value())
	{
		result.iMinConfirmedContrib = rWork.iConfirmedTick;
		result.iNewMinConfirmedContrib = rWork.iConfirmedTick;
		return result;
	}

	if (rWork.pendingFullState.has_value())
	{
		result.bHandled = false;
		Log(kLogNetwork, "CrcFastPathProcessCoord Pending full state forces reconcile Coord: ({},{})", rWork.coord.x, rWork.coord.y);
		result.iMinConfirmedContrib = rWork.iConfirmedTick;
		return result;
	}

	CrcValidateResult validateResult = CrcValidateLoop(rWork, iTargetTick);

	// Gap at confirmed+1 for this coord: first server update is non-consecutive.
	if (validateResult.iLastMatched == -1 && !rWork.serverUpdates.empty() && rWork.serverUpdates.begin()->first != rWork.iConfirmedTick + 1)
	{
		result.iMinConfirmedContrib = rWork.iConfirmedTick;
		result.iNewMinConfirmedContrib = rWork.iConfirmedTick;
		return result;
	}

	// No snapshots to validate: confirmed tick is at or past target tick.
	if (validateResult.iLastMatched == -1 && validateResult.bMatch && rWork.iConfirmedTick >= iTargetTick)
	{
		result.iMinConfirmedContrib = rWork.iConfirmedTick;
		result.iNewMinConfirmedContrib = rWork.iConfirmedTick;
		return result;
	}

	if (validateResult.iLastMatched >= 0)
	{
		CrcApplyMatchResult(rWork, validateResult.iLastMatched, validateResult.iLastMatchedIndex, rProfiling);

		result.iMinConfirmedContrib = rWork.iConfirmedTick;
		result.iNewMinConfirmedContrib = validateResult.iLastMatched;

		if (!validateResult.bMatch)
		{
			result.bHandled = false;
			Log(kLogNetwork, "CrcFastPathProcessCoord CRC mismatch after partial match Coord: ({},{}) LastMatched: {}", rWork.coord.x, rWork.coord.y, validateResult.iLastMatched);
		}
	}
	else if (!validateResult.bMatch)
	{
		result.bHandled = false;
		Log(kLogNetwork, "CrcFastPathProcessCoord CRC mismatch no matches Coord: ({},{})", rWork.coord.x, rWork.coord.y);
		result.iMinConfirmedContrib = rWork.iConfirmedTick;
	}
	else
	{
		if (rWork.iConfirmedTick + 1 < iTargetTick)
		{
			result.bHandled = false;
			Log(kLogNetwork, "CrcFastPathProcessCoord Snapshot missing Coord: ({},{}) Confirmed: {} Target: {}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick, iTargetTick);
			result.iMinConfirmedContrib = rWork.iConfirmedTick;
		}
		else
		{
			result.iMinConfirmedContrib = rWork.iConfirmedTick;
			result.iNewMinConfirmedContrib = rWork.iConfirmedTick;
		}
	}

	return result;
}

std::pair<bool, int64_t> ReconcileCrcFastPath(ReconcileContext& rReconcileContext)
{
	bool bAllHandled = true;
	int64_t iMinConfirmedTick = std::numeric_limits<int64_t>::max();
	int64_t iNewMinConfirmed = std::numeric_limits<int64_t>::max();

	int64_t iOldMinConfirmed = std::numeric_limits<int64_t>::max();
	for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.iConfirmedTick >= 0 && rWork.iConfirmedTick < iOldMinConfirmed)
		{
			iOldMinConfirmed = rWork.iConfirmedTick;
		}
	}

	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		CrcFastPathCoordResult coordResult = CrcFastPathProcessCoord(rWork, rReconcileContext.iTargetTick, rReconcileContext.profiling);

		if (!coordResult.bHandled)
		{
			bAllHandled = false;
		}
		if (coordResult.iMinConfirmedContrib >= 0 && coordResult.iMinConfirmedContrib < iMinConfirmedTick)
		{
			iMinConfirmedTick = coordResult.iMinConfirmedContrib;
		}
		if (coordResult.iNewMinConfirmedContrib >= 0 && coordResult.iNewMinConfirmedContrib < iNewMinConfirmed)
		{
			iNewMinConfirmed = coordResult.iNewMinConfirmedContrib;
		}
	}

	if (bAllHandled)
	{
		rReconcileContext.bCrcFastPathHandledAll = true;
		rReconcileContext.profiling.iCrcFastPathEvents = 1;
		rReconcileContext.newConfirmedHumanState = rReconcileContext.confirmedHumanState;
		rReconcileContext.humanGridCoord = rReconcileContext.confirmedHumanState.humanGridCoord;
		rReconcileContext.humanPlayerId = rReconcileContext.confirmedHumanState.humanPlayerId;
		rReconcileContext.fPreviousHumanArmor = rReconcileContext.confirmedHumanState.fPreviousHumanArmor;
		if (iOldMinConfirmed != std::numeric_limits<int64_t>::max() && iNewMinConfirmed > iOldMinConfirmed)
		{
			for (int64_t i = 0; i < iNewMinConfirmed - iOldMinConfirmed; ++i)
			{
				rReconcileContext.newConfirmedHumanState.fCurrentTime += kfDeltaTime;
			}
		}
	}

	return {bAllHandled, iMinConfirmedTick};
}

void ReconcileComputeActiveCoords(ReconcileContext& rReconcileContext)
{
	auto hasReplayStack = [&](engine::GridCoord coord) -> bool
	{
		CoordReconcileWork* pWork = rReconcileContext.FindCoordWork(coord);
		return pWork != nullptr && pWork->iReplayStackCount > 0;
	};

	// Human's cell plus existing neighbors
	rReconcileContext.activeCoords.clear();
	if (hasReplayStack(rReconcileContext.humanGridCoord))
	{
		rReconcileContext.activeCoords.push_back(rReconcileContext.humanGridCoord);
	}

	for (const engine::GridCoord& rOffset : engine::kNeighborOffsets)
	{
		engine::GridCoord neighbor {rReconcileContext.humanGridCoord.x + rOffset.x, rReconcileContext.humanGridCoord.y + rOffset.y};
		if (hasReplayStack(neighbor))
		{
			rReconcileContext.activeCoords.push_back(neighbor);
		}
	}
}

void ReconcileBuildFrameInput(ReconcileContext& rReconcileContext, [[maybe_unused]] int64_t iServerTick, const std::unordered_map<engine::GridCoord, engine::CoordFrames::CoordServerUpdate>& rCoordUpdates)
{
	rReconcileContext.frameInputs.clear();

	// Initialize frame inputs for all active coords that have replay stacks
	for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
	{
		CoordReconcileWork* pWork = rReconcileContext.FindCoordWork(rCoord);
		if (pWork == nullptr || pWork->iReplayStackCount <= 0)
		{
			continue;
		}
		rReconcileContext.frameInputs.try_emplace(rCoord);
	}

	// Copy status changes per coord
	for (const auto& [rCoord, rUpdate] : rCoordUpdates)
	{
		auto frameInputIt = rReconcileContext.frameInputs.find(rCoord);
		if (frameInputIt == rReconcileContext.frameInputs.end())
		{
			continue;
		}

		FrameInput& rFrameInput = frameInputIt->second;

		for (const StatusChange& rChange : rUpdate.statusChanges)
		{
			rFrameInput.statusChanges.push_back(rChange);
		}
	}
}

void ReconcileInjectPendingFullState([[maybe_unused]] ReconcileContext& rReconcileContext, CoordReconcileWork& rWork)
{
	// Move pending full state's Frame into workspace (workspace owns it, replay borrows pointer)
	rWork.replayWorkspace.push_back(std::move(rWork.pendingFullState->pFrame));
	rWork.replayStack.clear();
	rWork.replayStack.push_back(rWork.replayWorkspace.back().get());
	rWork.iReplayStackCount = 1;
	rWork.iReplayWorkspaceUsed = static_cast<int64_t>(rWork.replayWorkspace.size());
	rWork.pendingFullState.reset();
}

void ReconcilePruneInactiveFrames(ReconcileContext& rReconcileContext)
{
	ReconcileComputeActiveCoords(rReconcileContext);
	// Clear replay stacks for coords not in the active set
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (!std::ranges::contains(rReconcileContext.activeCoords, rWork.coord))
		{
			rWork.replayStack.clear();
			rWork.iReplayStackCount = 0;
			rWork.iReplayWorkspaceUsed = 0;
		}
	}
}

void ReconcileRollback(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick)
{
	// Only restore coords confirmed at iMinConfirmedTick; coords confirmed
	// at later frames are injected during replay/catch-up at their confirmed frame
	// to avoid running tick on states that already include those frames
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.iConfirmedTick <= iMinConfirmedTick)
		{
			ASSERT(rWork.iConfirmedOffset >= 0);
			int64_t iConfirmedPhysical = SnapshotIndex(rWork.iSnapshotHead, rWork.iConfirmedOffset);
			// Raw pointer from confirmed snapshot (no ownership transfer)
			rWork.replayStack.clear();
			rWork.replayStack.push_back(rWork.snapshots[iConfirmedPhysical].get());
			rWork.iReplayStackCount = 1;
			rWork.iReplayWorkspaceUsed = 0;
		}
	}

	rReconcileContext.humanGridCoord = rReconcileContext.confirmedHumanState.humanGridCoord;
	rReconcileContext.humanPlayerId = rReconcileContext.confirmedHumanState.humanPlayerId;
	rReconcileContext.fPreviousHumanArmor = rReconcileContext.confirmedHumanState.fPreviousHumanArmor;
	rReconcileContext.iTickCounter = iMinConfirmedTick;

	// Read fCurrentTime from the replay stack frame at iMinConfirmedTick
	// (confirmedHumanState.fCurrentTime may not match iMinConfirmedTick
	//  when coords were confirmed at different frame numbers)
	for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.iConfirmedTick == iMinConfirmedTick && rWork.iReplayStackCount > 0)
		{
			rReconcileContext.fCurrentTime = rWork.replayStack[0]->interpolate.fCurrentTime;
			break;
		}
	}

	// Inject pending full states at or before rollback frame
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick <= iMinConfirmedTick)
		{
			ReconcileInjectPendingFullState(rReconcileContext, rWork);
			Log(kLogNetwork, "ReconcileRollback Injected pending full state Coord: ({},{}) AtTick: {}", rWork.coord.x, rWork.coord.y, iMinConfirmedTick);
		}
	}
}

int64_t ReconcileFindReplayRange(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick)
{
	int64_t iReplayStart = iMinConfirmedTick + 1;
	int64_t iMaxConsecutive = iReplayStart - 1;
	for (int64_t iTick = iReplayStart; ; ++iTick)
	{
		bool bAnyCoordHasData = false;
		for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.serverUpdates.contains(iTick))
			{
				bAnyCoordHasData = true;
				break;
			}
		}
		if (!bAnyCoordHasData)
		{
			break;
		}
		iMaxConsecutive = iTick;
	}
	return iMaxConsecutive;
}

bool ReconcileInjectLateConfirmedCoord(CoordReconcileWork& rWork, int64_t iTick, int64_t iMinConfirmedTick)
{
	if (rWork.iConfirmedTick != iTick || rWork.iConfirmedTick <= iMinConfirmedTick)
	{
		return false;
	}

	ASSERT(rWork.iConfirmedOffset >= 0);
	int64_t iConfirmedPhysical = SnapshotIndex(rWork.iSnapshotHead, rWork.iConfirmedOffset);
	// Raw pointer from confirmed snapshot, reset workspace counter
	rWork.replayStack.clear();
	rWork.replayStack.push_back(rWork.snapshots[iConfirmedPhysical].get());
	rWork.iReplayStackCount = 1;
	rWork.iReplayWorkspaceUsed = 0;
	return true;
}

static std::unique_ptr<Frame> CloneFrameViaSerialization(const Frame& rFrame)
{
	std::ostringstream outputStream(std::ios::binary);
	outputStream << rFrame;
	std::istringstream inputStream(outputStream.str(), std::ios::binary);
	auto pClone = std::make_unique<Frame>();
	inputStream >> *pClone;
	return pClone;
}

bool ReconcileValidateCrcs(ReconcileContext& rReconcileContext, int64_t iTick, const std::unordered_map<engine::GridCoord, engine::CoordFrames::CoordServerUpdate>& rFrameCoordUpdates, const std::unordered_set<engine::GridCoord>& rGapCoords)
{
	for (const auto& [rCoord, rUpdate] : rFrameCoordUpdates)
	{
		CoordReconcileWork* pWork = rReconcileContext.FindCoordWork(rCoord);
		if (pWork == nullptr) { continue; }
		CoordReconcileWork& rWork = *pWork;
		if (rWork.iReplayStackCount <= 0)
		{
			continue;
		}

		if (rGapCoords.contains(rCoord))
		{
			continue;
		}

		// The latest result is at replayStack[iReplayStackCount - 1]
		Frame& rCurrentFrame = *rWork.replayStack[rWork.iReplayStackCount - 1];
		rCurrentFrame.interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
		common::crc_t clientCrc = rCurrentFrame.postRender.serverCrc;
		if (clientCrc != rUpdate.serverCrc)
		{
			char acServerCrc[20] {}, acClientCrc[20] {}, acPrevCrc[20] {};
			common::ToHex(std::span<char, 20>(acServerCrc), rUpdate.serverCrc);
			common::ToHex(std::span<char, 20>(acClientCrc), clientCrc);
			common::ToHex(std::span<char, 20>(acPrevCrc), rCurrentFrame.postRender.previousCrc);
			Log(kLogNetwork, "ReconcileValidateCrcs Desync Coord: ({},{}) Frame: {} ServerCrc: {} ClientCrc: {} PrevCrc: {}", rCoord.x, rCoord.y, iTick, acServerCrc, acClientCrc, acPrevCrc);
			{
				ScopedLogIndent scopedCrcIndent;
				char acServerInputCrc[20] {}, acClientInputCrc[20] {};
				common::ToHex(std::span<char, 20>(acServerInputCrc), rUpdate.inputCrc);
				auto frameInputIt = rReconcileContext.frameInputs.find(rCoord);
				common::crc_t clientInputCrcValue = (frameInputIt != rReconcileContext.frameInputs.end()) ? frameInputIt->second.ServerInputCrc() : 0;
				common::ToHex(std::span<char, 20>(acClientInputCrc), clientInputCrcValue);
				Log(kLogNetwork, "ServerInputCrc: {} ClientInputCrc: {}", acServerInputCrc, acClientInputCrc);
				LogStatusChangeList("Server StatusChanges", rUpdate.statusChanges);
				if (frameInputIt != rReconcileContext.frameInputs.end())
				{
					LogStatusChangeList("Client StatusChanges", frameInputIt->second.statusChanges);
				}
			}

			rReconcileContext.iDesyncTick = iTick;
			rReconcileContext.desyncCoord = rCoord;
			rReconcileContext.desyncServerCrc = rUpdate.serverCrc;
			rReconcileContext.desyncClientCrc = clientCrc;
			rReconcileContext.pDesyncClientFrame = CloneFrameViaSerialization(rCurrentFrame);
			return false;
		}

		// Validate input CRC
		auto inputIt = rReconcileContext.frameInputs.find(rCoord);
		if (inputIt != rReconcileContext.frameInputs.end())
		{
			common::crc_t clientInputCrc = inputIt->second.ServerInputCrc();
			if (clientInputCrc != rUpdate.inputCrc)
			{
				char acServerInputCrc[20] {}, acClientInputCrc[20] {};
				common::ToHex(std::span<char, 20>(acServerInputCrc), rUpdate.inputCrc);
				common::ToHex(std::span<char, 20>(acClientInputCrc), clientInputCrc);
				Log(kLogNetwork, "ReconcileValidateCrcs Input desync Coord: ({},{}) Frame: {} ServerInputCrc: {} ClientInputCrc: {}", rCoord.x, rCoord.y, iTick, acServerInputCrc, acClientInputCrc);
				{
					ScopedLogIndent scopedInputIndent;
					LogStatusChangeList("Server StatusChanges", rUpdate.statusChanges);
					LogStatusChangeList("Client StatusChanges", inputIt->second.statusChanges);
				}

				rReconcileContext.iDesyncTick = iTick;
				rReconcileContext.desyncCoord = rCoord;
				rReconcileContext.desyncServerCrc = rUpdate.inputCrc;
				rReconcileContext.desyncClientCrc = clientInputCrc;
				rReconcileContext.pDesyncClientFrame = CloneFrameViaSerialization(rCurrentFrame);
				return false;
			}
		}

		// Record CRC-validated index (deferred MOVE — stack entries are never overwritten)
		if (!rWork.bCrcFastPath)
		{
			rWork.iLastValidatedIndex = rWork.iReplayStackCount - 1;
			rWork.iNewConfirmedTick = iTick;
		}
	}

	return true;
}

void ReconcileReplay(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick, int64_t iMaxConsecutive)
{
	const int64_t iReplayStart = iMinConfirmedTick + 1;
	const int64_t iMaxReplay = (iMaxConsecutive - iMinConfirmedTick + 1) / 2;
	int64_t iReplayCount = 0;
	std::unordered_set<engine::GridCoord> gapCoords;

	for (int64_t iTick = iReplayStart; iTick <= iMaxConsecutive; ++iTick)
	{
		if (iReplayCount >= iMaxReplay || rReconcileContext.iTickCounter >= rReconcileContext.iTargetTick)
		{
			break;
		}

		ReconcileComputeActiveCoords(rReconcileContext);

		++rReconcileContext.iTickCounter;
		rReconcileContext.fCurrentTime += kfDeltaTime;

		// Gather per-coord server updates for this frame
		std::unordered_map<engine::GridCoord, engine::CoordFrames::CoordServerUpdate> frameCoordUpdates;
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			auto updateIt = rWork.serverUpdates.find(iTick);
			if (updateIt != rWork.serverUpdates.end())
			{
				frameCoordUpdates[rWork.coord] = std::move(updateIt->second);
				rWork.serverUpdates.erase(updateIt);
			}
		}

		// Detect per-coord server data gaps (coords replayed past confirmed frame without server data)
		for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (gapCoords.contains(rWork.coord))
			{
				continue;
			}
			if (iTick > rWork.iConfirmedTick && rWork.iReplayStackCount > 0 && !frameCoordUpdates.contains(rWork.coord))
			{
				gapCoords.insert(rWork.coord);
				Log(kLogNetwork, "ReconcileReplay Coord gap Coord: ({},{}) Tick: {} Confirmed: {}", rWork.coord.x, rWork.coord.y, iTick, rWork.iConfirmedTick);
			}
		}

		ReconcileBuildFrameInput(rReconcileContext, iTick, frameCoordUpdates);

		ReconcileRunTick(rReconcileContext);

		// Classify replay tick for profiling
		bool bHadStatusChanges = false;
		for (const auto& [rCoord, rUpdate] : frameCoordUpdates)
		{
			if (!rUpdate.statusChanges.empty())
			{
				bHadStatusChanges = true;
				break;
			}
		}
		if (bHadStatusChanges)
		{
			++rReconcileContext.profiling.iStatusChangeReplayTicks;
		}
		else
		{
			++rReconcileContext.profiling.iKnockOnReplayTicks;
		}

		// Inject pending full states at the matching transfer frame
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick == iTick)
			{
				ReconcileInjectPendingFullState(rReconcileContext, rWork);
				Log(kLogNetwork, "ReconcileReplay Injected pending full state Coord: ({},{}) Tick: {}", rWork.coord.x, rWork.coord.y, iTick);
			}
		}

		// Inject late-confirmed coords at their confirmed frame
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			ReconcileInjectLateConfirmedCoord(rWork, iTick, iMinConfirmedTick);
		}

		// CRC validation per coord that had server data
		if (!ReconcileValidateCrcs(rReconcileContext, iTick, frameCoordUpdates, gapCoords))
		{
			return;
		}

		++iReplayCount;
	}

	// After replay loop: extract validated frames into newSnapshots or record index
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.iLastValidatedIndex < 0 || rWork.bCrcFastPath)
		{
			continue;
		}

		if (rWork.iLastValidatedIndex == 0)
		{
			// Validated the confirmed snapshot itself (raw pointer from snapshots array)
			rWork.iNewConfirmedOffset = rWork.iConfirmedOffset;
		}
		else
		{
			// Validated a workspace entry — move to newSnapshots
			int64_t iWorkspaceIndex = rWork.iLastValidatedIndex - 1;
			rWork.newSnapshots.insert(rWork.newSnapshots.begin(), std::move(rWork.replayWorkspace[iWorkspaceIndex]));
			rWork.iNewConfirmedNewSnapshotIndex = 0;
		}
	}
}

void ReconcileCatchUp(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick)
{
	// Per-coord catch-up tracking: start workspace index and base frame for snapshot conversion
	struct CatchUpInfo
	{
		int64_t iStartWorkspaceIndex = 0;
		int64_t iBaseTick = 0;
	};

	// Heap: cold path, only runs when CRC fast-path fails
	std::vector<CatchUpInfo> catchUpInfos;
	catchUpInfos.reserve(rReconcileContext.coordWork.size());
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		catchUpInfos.push_back({rWork.iReplayWorkspaceUsed, rReconcileContext.iTickCounter});
	}

	while (rReconcileContext.iTickCounter < rReconcileContext.iTargetTick)
	{
		ReconcileComputeActiveCoords(rReconcileContext);

		++rReconcileContext.iTickCounter;
		rReconcileContext.fCurrentTime += kfDeltaTime;

		// Build empty inputs for catch-up
		rReconcileContext.frameInputs.clear();
		for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
		{
			CoordReconcileWork* pWork = rReconcileContext.FindCoordWork(rCoord);
			if (pWork == nullptr || pWork->iReplayStackCount <= 0)
			{
				continue;
			}
			rReconcileContext.frameInputs.try_emplace(rCoord);
		}

		ReconcileRunTick(rReconcileContext);
		++rReconcileContext.profiling.iAssumedFrameTicks;

		// Inject late-confirmed coords at their confirmed frame
		for (size_t iWorkIndex = 0; iWorkIndex < rReconcileContext.coordWork.size(); ++iWorkIndex)
		{
			CoordReconcileWork& rWork = rReconcileContext.coordWork.at(iWorkIndex);
			if (ReconcileInjectLateConfirmedCoord(rWork, rReconcileContext.iTickCounter, iMinConfirmedTick))
			{
				catchUpInfos[iWorkIndex] = {0, rReconcileContext.iTickCounter};
			}
		}
	}

	// Convert accumulated workspace entries to snapshots for CRC fast-path
	for (size_t iWorkIndex = 0; iWorkIndex < rReconcileContext.coordWork.size(); ++iWorkIndex)
	{
		CoordReconcileWork& rWork = rReconcileContext.coordWork.at(iWorkIndex);
		const CatchUpInfo& rInfo = catchUpInfos[iWorkIndex];

		for (int64_t i = rInfo.iStartWorkspaceIndex; i < rWork.iReplayWorkspaceUsed; ++i)
		{
			if (rWork.replayWorkspace[i] == nullptr)
			{
				continue;
			}

			rWork.replayWorkspace[i]->interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);

			rWork.newSnapshots.push_back(std::move(rWork.replayWorkspace[i]));
		}
	}
}

#endif // BT_CLIENT

} // namespace game
