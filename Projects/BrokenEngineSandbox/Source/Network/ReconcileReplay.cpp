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
};

static CrcFastPathCoordResult CrcFastPathProcessCoord(CoordReconcileWork& rWork, int64_t iTargetTick, ReconcileContext::Profiling& rProfiling)
{
	CrcFastPathCoordResult result;

	if (rWork.serverUpdates.empty() && !rWork.pendingFullState.has_value())
	{
		return result;
	}

	if (rWork.pendingFullState.has_value())
	{
		result.bHandled = false;
		Log(kLogNetwork, "CrcFastPathProcessCoord Pending full state forces reconcile Coord: ({},{})", rWork.coord.x, rWork.coord.y);
		return result;
	}

	CrcValidateResult validateResult = CrcValidateLoop(rWork, iTargetTick);

	// Gap at confirmed+1 for this coord: first server update is non-consecutive.
	if (validateResult.iLastMatched == -1 && !rWork.serverUpdates.empty() && rWork.serverUpdates.begin()->first != rWork.iConfirmedTick + 1)
	{
		return result;
	}

	// No snapshots to validate: confirmed tick is at or past target tick.
	if (validateResult.iLastMatched == -1 && validateResult.bMatch && rWork.iConfirmedTick >= iTargetTick)
	{
		return result;
	}

	if (validateResult.iLastMatched >= 0)
	{
		CrcApplyMatchResult(rWork, validateResult.iLastMatched, validateResult.iLastMatchedIndex, rProfiling);

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
	}
	else
	{
		if (rWork.iConfirmedTick + 1 < iTargetTick)
		{
			result.bHandled = false;
			Log(kLogNetwork, "CrcFastPathProcessCoord Snapshot missing Coord: ({},{}) Confirmed: {} Target: {}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick, iTargetTick);
		}
	}

	return result;
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

// --- Per-coord reconciliation functions ---

static void ReconcileRollbackCoord(CoordReconcileWork& rWork)
{
	ASSERT(rWork.iConfirmedOffset >= 0);
	int64_t iConfirmedPhysical = SnapshotIndex(rWork.iSnapshotHead, rWork.iConfirmedOffset);
	rWork.replayStack.clear();
	rWork.replayStack.push_back(rWork.snapshots[iConfirmedPhysical].get());
	rWork.iReplayStackCount = 1;
	rWork.iReplayWorkspaceUsed = 0;
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

static void ReconcileRunTickCoord(CoordReconcileWork& rWork, int64_t iTick, float fTime, FrameInput& rFrameInput)
{
	// Ensure workspace frame exists
	int64_t iNextWorkspace = rWork.iReplayWorkspaceUsed;
	if (iNextWorkspace >= static_cast<int64_t>(rWork.replayWorkspace.size()))
	{
		rWork.replayWorkspace.resize(static_cast<size_t>(iNextWorkspace + 1));
	}
	if (rWork.replayWorkspace[iNextWorkspace] == nullptr)
	{
		rWork.replayWorkspace[iNextWorkspace] = std::make_unique<Frame>();
	}

	Frame* pCurrent = rWork.replayStack[rWork.iReplayStackCount - 1];
	Frame* pNext = rWork.replayWorkspace[iNextWorkspace].get();

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
		}
	}
	std::erase_if(rFrameInput.statusChanges, [](const StatusChange& rStatusChange)
	{
		return IsTransferType(rStatusChange.eType);
	});

	if (bHadTransfers)
	{
		pNext->postRender.serverCrc = pNext->ServerCrc();
		pNext->postRender.crc = pNext->Crc();
	}

	// Advance replay stack
	rWork.replayStack.push_back(pNext);
	rWork.iReplayStackCount++;
	rWork.iReplayWorkspaceUsed++;
}

static bool ReconcileValidateCrcCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork, int64_t iTick, const engine::CoordFrames::CoordServerUpdate& rUpdate, const FrameInput& rFrameInput)
{
	Frame& rCurrentFrame = *rWork.replayStack[rWork.iReplayStackCount - 1];
	rCurrentFrame.interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
	common::crc_t clientCrc = rCurrentFrame.postRender.serverCrc;

	if (clientCrc != rUpdate.serverCrc)
	{
		char acServerCrc[20] {}, acClientCrc[20] {}, acPrevCrc[20] {};
		common::ToHex(std::span<char, 20>(acServerCrc), rUpdate.serverCrc);
		common::ToHex(std::span<char, 20>(acClientCrc), clientCrc);
		common::ToHex(std::span<char, 20>(acPrevCrc), rCurrentFrame.postRender.previousCrc);
		Log(kLogNetwork, "ReconcileValidateCrcCoord Desync Coord: ({},{}) Frame: {} ServerCrc: {} ClientCrc: {} PrevCrc: {}", rWork.coord.x, rWork.coord.y, iTick, acServerCrc, acClientCrc, acPrevCrc);
		{
			ScopedLogIndent scopedCrcIndent;
			char acServerInputCrc[20] {}, acClientInputCrc[20] {};
			common::ToHex(std::span<char, 20>(acServerInputCrc), rUpdate.inputCrc);
			common::ToHex(std::span<char, 20>(acClientInputCrc), rFrameInput.ServerInputCrc());
			Log(kLogNetwork, "ServerInputCrc: {} ClientInputCrc: {}", acServerInputCrc, acClientInputCrc);
			LogStatusChangeList("Server StatusChanges", rUpdate.statusChanges);
			LogStatusChangeList("Client StatusChanges", rFrameInput.statusChanges);
		}

		rReconcileContext.iDesyncTick = iTick;
		rReconcileContext.desyncCoord = rWork.coord;
		rReconcileContext.desyncServerCrc = rUpdate.serverCrc;
		rReconcileContext.desyncClientCrc = clientCrc;
		rReconcileContext.pDesyncClientFrame = CloneFrameViaSerialization(rCurrentFrame);
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

		rReconcileContext.iDesyncTick = iTick;
		rReconcileContext.desyncCoord = rWork.coord;
		rReconcileContext.desyncServerCrc = rUpdate.inputCrc;
		rReconcileContext.desyncClientCrc = clientInputCrc;
		rReconcileContext.pDesyncClientFrame = CloneFrameViaSerialization(rCurrentFrame);
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
	const int64_t iMaxReplay = (iMaxConsecutive - rWork.iConfirmedTick + 1) / 2;
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

		// Build FrameInput from server StatusChanges
		FrameInput frameInput;
		for (const StatusChange& rChange : updateIt->second.statusChanges)
		{
			frameInput.statusChanges.push_back(rChange);
		}

		ReconcileRunTickCoord(rWork, iTick, rfTime, frameInput);

		// Inject pending full state at matching tick
		if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick == iTick)
		{
			ReconcileInjectPendingFullState(rReconcileContext, rWork);
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
			++rReconcileContext.profiling.iStatusChangeReplayTicks;
		}
		else
		{
			++rReconcileContext.profiling.iKnockOnReplayTicks;
		}

		++iReplayCount;
	}

	// Extract validated frames into newSnapshots
	if (rWork.iLastValidatedIndex >= 0)
	{
		if (rWork.iLastValidatedIndex == 0)
		{
			// Validated the confirmed snapshot itself
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

static void ReconcileCatchUpCoord(CoordReconcileWork& rWork, int64_t iTargetTick, float& rfTime, ReconcileContext::Profiling& rProfiling)
{
	int64_t iStartWorkspaceIndex = rWork.iReplayWorkspaceUsed;

	// Determine current tick from the replay stack tip
	int64_t iCurrentTick = rWork.replayStack[rWork.iReplayStackCount - 1]->interpolate.iTick;

	while (iCurrentTick < iTargetTick)
	{
		++iCurrentTick;
		rfTime += kfDeltaTime;

		FrameInput emptyInput;
		ReconcileRunTickCoord(rWork, iCurrentTick, rfTime, emptyInput);
		++rProfiling.iAssumedFrameTicks;
	}

	// Convert accumulated workspace entries to snapshots
	for (int64_t i = iStartWorkspaceIndex; i < rWork.iReplayWorkspaceUsed; ++i)
	{
		if (rWork.replayWorkspace[i] == nullptr)
		{
			continue;
		}

		rWork.replayWorkspace[i]->interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
		rWork.newSnapshots.push_back(std::move(rWork.replayWorkspace[i]));
	}
}

void ReconcileCoord(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork)
{
	// Try CRC fast path first
	CrcFastPathCoordResult fastPathResult = CrcFastPathProcessCoord(rWork, rReconcileContext.iTargetTick, rReconcileContext.profiling);
	if (fastPathResult.bHandled)
	{
		++rReconcileContext.profiling.iCrcFastPathEvents;
		return;
	}

	// Partial CRC match may have set fast-path output fields — reset them for full replay
	rWork.bCrcFastPath = false;
	rWork.iNewConfirmedTick = -1;
	rWork.iNewConfirmedOffset = -1;
	rWork.newSnapshots.clear();

	rReconcileContext.bAnyFullReplay = true;

	Log(kLogNetwork, "ReconcileCoord Full replay Coord: ({},{}) Confirmed: {} Target: {}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick, rReconcileContext.iTargetTick);

	ReconcileRollbackCoord(rWork);
	float fTime = rWork.replayStack[0]->interpolate.fCurrentTime;

	// Inject pending full state at or before confirmed frame
	if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick <= rWork.iConfirmedTick)
	{
		ReconcileInjectPendingFullState(rReconcileContext, rWork);
		fTime = rWork.replayStack[0]->interpolate.fCurrentTime;
		Log(kLogNetwork, "ReconcileCoord Injected pending full state Coord: ({},{}) AtTick: {}", rWork.coord.x, rWork.coord.y, rWork.iConfirmedTick);
	}

	int64_t iMaxConsecutive = ReconcileFindReplayRangeCoord(rWork);

	Log(kLogNetwork, "ReconcileCoord ReplayRange Coord: ({},{}) MaxConsecutive: {} Size: {}", rWork.coord.x, rWork.coord.y, iMaxConsecutive, iMaxConsecutive - rWork.iConfirmedTick);

	ReconcileReplayCoord(rReconcileContext, rWork, iMaxConsecutive, fTime);
	if (rReconcileContext.iDesyncTick >= 0)
	{
		return;
	}

	ReconcileCatchUpCoord(rWork, rReconcileContext.iTargetTick, fTime, rReconcileContext.profiling);

	// Set context counters from this coord's final state
	rReconcileContext.iTickCounter = rReconcileContext.iTargetTick;
	if (rWork.coord == rReconcileContext.confirmedHumanState.humanGridCoord)
	{
		rReconcileContext.fCurrentTime = fTime;
	}
}

void ReconcileUpdateHumanState(ReconcileContext& rReconcileContext)
{
	ConfirmedHumanState humanState = rReconcileContext.confirmedHumanState;

	// Advance fCurrentTime based on human coord's reconciliation result
	for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.coord != humanState.humanGridCoord)
		{
			continue;
		}

		if (!rWork.bCrcFastPath && rWork.iReplayStackCount > 0)
		{
			// Human coord did full replay: use replay tip's fCurrentTime
			humanState.fCurrentTime = rWork.replayStack[rWork.iReplayStackCount - 1]->interpolate.fCurrentTime;
		}
		else
		{
			// Human coord fast-pathed: advance by confirmed tick delta
			int64_t iNewTick = (rWork.iNewConfirmedTick >= 0) ? rWork.iNewConfirmedTick : rWork.iConfirmedTick;
			int64_t iAdvancement = iNewTick - rWork.iConfirmedTick;
			for (int64_t i = 0; i < iAdvancement; ++i)
			{
				humanState.fCurrentTime += kfDeltaTime;
			}
		}
		break;
	}

	if (rReconcileContext.bAnyFullReplay)
	{

		// Scan full-replay coords for human migration via transfer requests
		for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.bCrcFastPath)
			{
				continue;
			}

			// replayStack[0] is the confirmed frame; scan from index 1 onwards
			for (int64_t i = 1; i < rWork.iReplayStackCount; ++i)
			{
				const Frame& rFrame = *rWork.replayStack[i];
				for (const TransferRequest& rRequest : rFrame.postRender.transferRequests)
				{
					if (rRequest.eType != StatusChangeType::kTransferPlayer)
					{
						continue;
					}
					if (!humanState.humanPlayerId.IsValid())
					{
						continue;
					}
					if (rRequest.iEntityId != humanState.humanPlayerId.ToUuid().Value())
					{
						continue;
					}

					engine::GridCoord destination {rWork.coord.x + rRequest.iDeltaX, rWork.coord.y + rRequest.iDeltaY};
					humanState.humanGridCoord = destination;
					humanState.fPreviousHumanArmor = rRequest.data.fHealth;

					// Find human's new player ID by position matching in destination coord
					for (const CoordReconcileWork& rDestWork : rReconcileContext.coordWork)
					{
						if (rDestWork.coord != destination || rDestWork.iReplayStackCount <= 0)
						{
							continue;
						}

						const Frame& rDestFrame = *rDestWork.replayStack[rDestWork.iReplayStackCount - 1];
						bool bFound = false;
						for (int64_t j = 0; j < rDestFrame.postRender.pPlayers->iCount; ++j)
						{
							if (XMVector4Equal(rDestFrame.interpolate.pPlayers->pVecPositions[j], rRequest.data.vecPosition))
							{
								humanState.humanPlayerId = rDestFrame.postRender.pPlayers->puiIds[j];
								bFound = true;
								break;
							}
						}
						if (!bFound && rDestFrame.postRender.pPlayers->iCount > 0)
						{
							humanState.humanPlayerId = rDestFrame.postRender.pPlayers->puiIds[rDestFrame.postRender.pPlayers->iCount - 1];
						}
						break;
					}
				}
			}
		}
	}

	rReconcileContext.newConfirmedHumanState = humanState;
}

#endif // BT_CLIENT

} // namespace game
