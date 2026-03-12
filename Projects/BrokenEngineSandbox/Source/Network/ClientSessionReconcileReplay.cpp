#include "Game.h"

#include "Network/ClientSession.h"
#include "Frame/FrameTick.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Spaceships/Spaceships.h"

namespace game
{

#if defined(BT_CLIENT)

void ClientSession::ReconcileRunTick(ReconcileContext& rReconcileContext)
{
	const int64_t iActiveCount = static_cast<int64_t>(rReconcileContext.activeCoords.size());
	const std::unordered_map<engine::GridCoord, size_t>& rCoordWorkIndex = rReconcileContext.coordWorkIndex;

	// Ensure workspace frames exist for each active coord
	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		const engine::GridCoord& rCoord = rReconcileContext.activeCoords.at(static_cast<size_t>(j));
		auto indexIt = rReconcileContext.coordWorkIndex.find(rCoord);
		if (indexIt == rCoordWorkIndex.end())
		{
			continue;
		}
		CoordReconcileWork& rWork = rReconcileContext.coordWork.at(indexIt->second);
		int64_t iNextWorkspace = rWork.iReplayWorkspaceUsed;
		if (iNextWorkspace >= static_cast<int64_t>(rWork.replayWorkspace.size()))
		{
			rWork.replayWorkspace.resize(static_cast<size_t>(iNextWorkspace + 1));
		}
		if (rWork.replayWorkspace[iNextWorkspace] == nullptr)
		{
			rWork.replayWorkspace[iNextWorkspace] = std::make_unique<Frame>();
		}
	}

	common::gpThreadLocal->mWorkbuffer.Push();
	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		const engine::GridCoord& rCoord = rReconcileContext.activeCoords.at(static_cast<size_t>(j));
		auto indexIt = rReconcileContext.coordWorkIndex.find(rCoord);
		if (indexIt == rCoordWorkIndex.end())
		{
			continue;
		}
		CoordReconcileWork& rWork = rReconcileContext.coordWork.at(indexIt->second);
		Frame* pCurrent = rWork.replayStack[rWork.iReplayStackCount - 1];
		Frame* pNext = rWork.replayWorkspace[rWork.iReplayWorkspaceUsed].get();
		common::gpThreadLocal->mWorkbuffer.PushBack<ActiveFrameRef>({
			.pNext = pNext,
			.pCurrent = pCurrent,
			.pFrameInput = &rReconcileContext.frameInputs.at(rCoord),
		});
	}
	std::span<const ActiveFrameRef> activeFrameRefs = common::gpThreadLocal->mWorkbuffer.Span<ActiveFrameRef>();
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

		for (const StatusChange& rStatusChange : rFrameInput.statusChanges)
		{
			if (IsTransferType(rStatusChange.eType))
			{
				SpawnTransfer(rNext, rStatusChange.eType, rStatusChange.data, rNext.postRender.playerAlignment);
			}
		}
		std::erase_if(rFrameInput.statusChanges, [](const StatusChange& rStatusChange)
		{
			return IsTransferType(rStatusChange.eType);
		});
	}

	common::gpThreadLocal->mWorkbuffer.Pop();

	// Track human player grid migration (read-only, no cross-coord spawning)
	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		const engine::GridCoord& rCoord = rReconcileContext.activeCoords.at(static_cast<size_t>(j));
		auto indexIt = rReconcileContext.coordWorkIndex.find(rCoord);
		if (indexIt == rCoordWorkIndex.end())
		{
			continue;
		}
		CoordReconcileWork& rWork = rReconcileContext.coordWork.at(indexIt->second);
		const Frame& rNext = *rWork.replayWorkspace[rWork.iReplayWorkspaceUsed];
		for (const TransferRequest& rRequest : rNext.postRender.transferRequests)
		{
			if (rRequest.eType == StatusChangeType::kTransferPlayer &&
				rReconcileContext.humanPlayerId.IsValid() &&
				rRequest.iEntityId == rReconcileContext.humanPlayerId.ToUuid().Value())
			{
				engine::GridCoord destination {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};
				rReconcileContext.humanGridCoord = destination;

				// Find human's new ID by position-matching in destination frame (server already spawned there)
				auto destIndexIt = rCoordWorkIndex.find(destination);
				if (destIndexIt != rCoordWorkIndex.end())
				{
					CoordReconcileWork& rDestWork = rReconcileContext.coordWork.at(destIndexIt->second);
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

	// Push workspace frame onto replay stack and advance counts
	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		const engine::GridCoord& rCoord = rReconcileContext.activeCoords.at(static_cast<size_t>(j));
		auto indexIt = rReconcileContext.coordWorkIndex.find(rCoord);
		if (indexIt == rCoordWorkIndex.end())
		{
			continue;
		}
		CoordReconcileWork& rWork = rReconcileContext.coordWork.at(indexIt->second);
		rWork.replayStack.push_back(rWork.replayWorkspace[rWork.iReplayWorkspaceUsed].get());
		rWork.iReplayStackCount++;
		rWork.iReplayWorkspaceUsed++;
	}

	for (auto& [rCoord, rFrameInput] : rReconcileContext.frameInputs)
	{
		rFrameInput.statusChanges.clear();
	}
}

void ClientSession::ReconcileComputeActiveCoords(ReconcileContext& rReconcileContext)
{
	const std::unordered_map<engine::GridCoord, size_t>& rCoordWorkIndex = rReconcileContext.coordWorkIndex;

	auto hasReplayStack = [&](engine::GridCoord coord) -> bool
	{
		auto it = rCoordWorkIndex.find(coord);
		if (it == rCoordWorkIndex.end())
		{
			return false;
		}
		return rReconcileContext.coordWork.at(it->second).iReplayStackCount > 0;
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

void ClientSession::ReconcileBuildFrameInput(ReconcileContext& rReconcileContext, [[maybe_unused]] int64_t iServerTick, const std::unordered_map<engine::GridCoord, engine::CoordFrames::CoordServerUpdate>& rCoordUpdates)
{
	rReconcileContext.frameInputs.clear();

	// Initialize frame inputs for all active coords that have replay stacks
	for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
	{
		auto indexIt = rReconcileContext.coordWorkIndex.find(rCoord);
		if (indexIt == rReconcileContext.coordWorkIndex.end())
		{
			continue;
		}
		if (rReconcileContext.coordWork.at(indexIt->second).iReplayStackCount <= 0)
		{
			continue;
		}
		rReconcileContext.frameInputs[rCoord];
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

void ClientSession::ReconcileInjectPendingFullState([[maybe_unused]] ReconcileContext& rReconcileContext, CoordReconcileWork& rWork)
{
	common::Log("GameClient: InjectPendingFullState coord ({},{}) tick={}", rWork.coord.x, rWork.coord.y, rWork.pendingFullState->iTick); // DT: TEMP
	// Move pending full state's Frame into workspace (workspace owns it, replay borrows pointer)
	rWork.replayWorkspace.push_back(std::move(rWork.pendingFullState->pFrame));
	rWork.replayStack.clear();
	rWork.replayStack.push_back(rWork.replayWorkspace.back().get());
	rWork.iReplayStackCount = 1;
	rWork.iReplayWorkspaceUsed = static_cast<int64_t>(rWork.replayWorkspace.size());
	rWork.pendingFullState.reset();
}

void ClientSession::ReconcilePruneInactiveFrames(ReconcileContext& rReconcileContext)
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

void ClientSession::ReconcileRollback(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick)
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

	common::Log("GameClient: Rollback minConfirmed={} humanCoord ({},{}) fCurrentTime={}", iMinConfirmedTick, rReconcileContext.humanGridCoord.x, rReconcileContext.humanGridCoord.y, rReconcileContext.fCurrentTime); // DT: TEMP

	// Inject pending full states at or before rollback frame
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick <= iMinConfirmedTick)
		{
			ReconcileInjectPendingFullState(rReconcileContext, rWork);
		}
	}
}

int64_t ClientSession::ReconcileFindReplayRange(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick)
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

void ClientSession::ReconcileReplay(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick, int64_t iMaxConsecutive)
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
				common::Log("GameClient: Replay gap coord ({},{}) tick={}", rWork.coord.x, rWork.coord.y, iTick); // DT: TEMP
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
			++rReconcileContext.iStatusChangeReplayTicks;
		}
		else
		{
			++rReconcileContext.iKnockOnReplayTicks;
		}

		// Inject pending full states at the matching transfer frame
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.pendingFullState.has_value() && rWork.pendingFullState->iTick == iTick)
			{
				ReconcileInjectPendingFullState(rReconcileContext, rWork);
			}
		}

		// Inject late-confirmed coords at their confirmed frame
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.iConfirmedTick == iTick && rWork.iConfirmedTick > iMinConfirmedTick)
			{
				ASSERT(rWork.iConfirmedOffset >= 0);
				int64_t iConfirmedPhysical = SnapshotIndex(rWork.iSnapshotHead, rWork.iConfirmedOffset);
				// Raw pointer from confirmed snapshot, reset workspace counter
				rWork.replayStack.clear();
				rWork.replayStack.push_back(rWork.snapshots[iConfirmedPhysical].get());
				rWork.iReplayStackCount = 1;
				rWork.iReplayWorkspaceUsed = 0;
			}
		}

		// CRC validation per coord that had server data
		for (const auto& [rCoord, rUpdate] : frameCoordUpdates)
		{
			auto indexIt = rReconcileContext.coordWorkIndex.find(rCoord);
			if (indexIt == rReconcileContext.coordWorkIndex.end())
			{
				continue;
			}

			CoordReconcileWork& rWork = rReconcileContext.coordWork.at(indexIt->second);
			if (rWork.iReplayStackCount <= 0)
			{
				continue;
			}

			if (gapCoords.contains(rCoord))
			{
				continue;
			}

			// The latest result is at replayStack[iReplayStackCount - 1]
			Frame& rCurrentFrame = *rWork.replayStack[rWork.iReplayStackCount - 1];
			rCurrentFrame.interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
			common::crc_t clientCrc = rCurrentFrame.postRender.serverCrc;
			if (clientCrc != rUpdate.serverCrc)
			{
				common::Log("Reconcile desync at ({},{}): server={} client={} frame={}", rCoord.x, rCoord.y, rUpdate.serverCrc, clientCrc, iTick);
				FILE_LOG(0, "[Reconcile] Desync at ({},{}): server={} client={} frame={}", rCoord.x, rCoord.y, rUpdate.serverCrc, clientCrc, iTick);

				// Log input CRC regardless of frame CRC result
				auto inputCheckIt = rReconcileContext.frameInputs.find(rCoord);
				if (inputCheckIt != rReconcileContext.frameInputs.end())
				{
					common::crc_t clientInputCrc = inputCheckIt->second.ServerInputCrc();
					FILE_LOG(0, "[Reconcile] InputCRC at ({},{}): server={} client={} frame={} match={}", rCoord.x, rCoord.y, rUpdate.inputCrc, clientInputCrc, iTick, clientInputCrc == rUpdate.inputCrc);
				}

				rReconcileContext.iDesyncTick = iTick;
				rReconcileContext.desyncCoord = rCoord;
				rReconcileContext.desyncServerCrc = rUpdate.serverCrc;
				rReconcileContext.desyncClientCrc = clientCrc;

				std::ostringstream outputStream(std::ios::binary);
				outputStream << rCurrentFrame;
				std::istringstream inputStream(outputStream.str(), std::ios::binary);
				rReconcileContext.pDesyncClientFrame = std::make_unique<Frame>();
				inputStream >> *rReconcileContext.pDesyncClientFrame;
				return;
			}

			// Validate input CRC
			auto inputIt = rReconcileContext.frameInputs.find(rCoord);
			if (inputIt != rReconcileContext.frameInputs.end())
			{
				common::crc_t clientInputCrc = inputIt->second.ServerInputCrc();
				if (clientInputCrc != rUpdate.inputCrc)
				{
					common::Log("Reconcile input desync at ({},{}): server={} client={} frame={}", rCoord.x, rCoord.y, rUpdate.inputCrc, clientInputCrc, iTick);
					FILE_LOG(0, "[Reconcile] Input desync at ({},{}): server={} client={} frame={}", rCoord.x, rCoord.y, rUpdate.inputCrc, clientInputCrc, iTick);

					rReconcileContext.iDesyncTick = iTick;
					rReconcileContext.desyncCoord = rCoord;
					rReconcileContext.desyncServerCrc = rUpdate.inputCrc;
					rReconcileContext.desyncClientCrc = clientInputCrc;

					std::ostringstream outputStream(std::ios::binary);
					outputStream << rCurrentFrame;
					std::istringstream inputStream(outputStream.str(), std::ios::binary);
					rReconcileContext.pDesyncClientFrame = std::make_unique<Frame>();
					inputStream >> *rReconcileContext.pDesyncClientFrame;
					return;
				}
			}

			// Record CRC-validated index (deferred MOVE — stack entries are never overwritten)
			if (!rWork.bCrcFastPath)
			{
				rWork.iLastValidatedIndex = rWork.iReplayStackCount - 1;
				rWork.iNewConfirmedTick = iTick;
			}
		}

		++iReplayCount;
	}

	common::Log("GameClient: Replay complete replayCount={} gapCoords={}", iReplayCount, gapCoords.size()); // DT: TEMP

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

void ClientSession::ReconcileCatchUp(ReconcileContext& rReconcileContext, int64_t iMinConfirmedTick)
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
			auto indexIt = rReconcileContext.coordWorkIndex.find(rCoord);
			if (indexIt == rReconcileContext.coordWorkIndex.end())
			{
				continue;
			}
			if (rReconcileContext.coordWork.at(indexIt->second).iReplayStackCount <= 0)
			{
				continue;
			}
			rReconcileContext.frameInputs[rCoord];
		}

		ReconcileRunTick(rReconcileContext);
		++rReconcileContext.iAssumedFrameTicks;

		// Inject late-confirmed coords at their confirmed frame
		for (size_t iWorkIndex = 0; iWorkIndex < rReconcileContext.coordWork.size(); ++iWorkIndex)
		{
			CoordReconcileWork& rWork = rReconcileContext.coordWork.at(iWorkIndex);
			if (rWork.iConfirmedTick == rReconcileContext.iTickCounter && rWork.iConfirmedTick > iMinConfirmedTick)
			{
				ASSERT(rWork.iConfirmedOffset >= 0);
				int64_t iConfirmedPhysical = SnapshotIndex(rWork.iSnapshotHead, rWork.iConfirmedOffset);
				// Raw pointer from confirmed snapshot, reset workspace counter
				rWork.replayStack.clear();
				rWork.replayStack.push_back(rWork.snapshots[iConfirmedPhysical].get());
				rWork.iReplayStackCount = 1;
				rWork.iReplayWorkspaceUsed = 0;
				// Reset tracking since this coord's stack was reset
				catchUpInfos[iWorkIndex] = {0, rReconcileContext.iTickCounter};
			}
		}
	}

	common::Log("GameClient: CatchUp from={} to={} ticks={}", iMinConfirmedTick, rReconcileContext.iTargetTick, rReconcileContext.iAssumedFrameTicks); // DT: TEMP

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
