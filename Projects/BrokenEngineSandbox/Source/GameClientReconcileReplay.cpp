#include "Game.h"

#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Spaceships/Spaceships.h"

namespace game
{

#ifdef BT_CLIENT

namespace
{

struct ActiveFrameRef
{
	Frame* pNext = nullptr;
	Frame* pCurrent = nullptr;
	FrameInput* pFrameInput = nullptr;
};

} // namespace

void Game::ReconcileRunPhysics(ReconcileContext& rReconcileContext)
{
	const int64_t iActiveCount = static_cast<int64_t>(rReconcileContext.activeCoords.size());

	common::gpThreadLocal->mWorkbuffer.Push();
	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		const engine::GridCoord& rCoord = rReconcileContext.activeCoords.at(static_cast<size_t>(j));
		common::gpThreadLocal->mWorkbuffer.PushBack<ActiveFrameRef>({
			.pNext = rReconcileContext.nextFrames.at(rCoord).get(),
			.pCurrent = rReconcileContext.currentFrames.at(rCoord).get(),
			.pFrameInput = &rReconcileContext.frameInputs.at(rCoord),
		});
	}
	std::span<const ActiveFrameRef> activeFrameRefs = common::gpThreadLocal->mWorkbuffer.Span<ActiveFrameRef>();

	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		Frame& rNext = *activeFrameRefs[j].pNext;
		const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
		FrameInterpolate::AllocateAndCopy(rNext.interpolate, rCurrent.interpolate);
		FrameInterpolate::Update(rNext.interpolate, rCurrent, kfDeltaTime);
		rNext.interpolate.iFrame = rReconcileContext.iFrameCounter;
		rNext.interpolate.fCurrentTime = rReconcileContext.fCurrentTime;
		rNext.interpolate.frameFlags.Set(engine::FrameFlags::kRecalculated);
	}

	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		Frame& rNext = *activeFrameRefs[j].pNext;
		const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
		FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;
		FramePostRender::AllocateAndCopy(rNext.postRender, rCurrent.postRender);
		FramePostRender::Update(rNext, rCurrent, rFrameInput);
	}

	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		Frame& rNext = *activeFrameRefs[j].pNext;
		const Frame& rCurrent = *activeFrameRefs[j].pCurrent;
		FramePostRender::PreCollision(rNext, rCurrent);
		engine::Collision::Collide(rNext.postRender.alignments, rNext.postRender.vecArea);
		FramePostRender::PostCollision(rNext, rCurrent);
		FramePostRender::AreaDamage(rNext, rCurrent);
	}

	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		FramePostRender::Transfer(*activeFrameRefs[j].pNext);
	}

	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		Frame& rNext = *activeFrameRefs[j].pNext;
		FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;
		FramePostRender::Destroy(rNext);
		FramePostRender::Spawn(rNext, rFrameInput);
	}

	// Apply server-provided transfer StatusChanges per-coord (no cross-coord coupling)
	// Runs after Destroy/Spawn to match server ordering (HarvestTransfers runs after Destroy/Spawn)
	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		Frame& rNext = *activeFrameRefs[j].pNext;
		FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;

		// Sort transfer StatusChanges by server sequence for deterministic ordering
		std::ranges::sort(rFrameInput.statusChanges, [](const StatusChange& rLeft, const StatusChange& rRight)
		{
			if (IsTransferType(rLeft.eType) != IsTransferType(rRight.eType))
			{
				return IsTransferType(rLeft.eType) && !IsTransferType(rRight.eType);
			}
			if (IsTransferType(rLeft.eType) && IsTransferType(rRight.eType))
			{
				return rLeft.uiSequence < rRight.uiSequence;
			}
			return false;
		});

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
		const Frame& rNext = *rReconcileContext.nextFrames.at(rCoord);
		for (const TransferRequest& rRequest : rNext.postRender.transferRequests)
		{
			if (rRequest.eType == StatusChangeType::kTransferPlayer &&
				rReconcileContext.humanPlayerId.IsValid() &&
				rRequest.iEntityId == rReconcileContext.humanPlayerId.ToUuid().Value())
			{
				engine::GridCoord destination {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};
				rReconcileContext.humanGridCoord = destination;

				// Find human's new ID by position-matching in destination frame (server already spawned there)
				auto destinationIt = rReconcileContext.nextFrames.find(destination);
				if (destinationIt != rReconcileContext.nextFrames.end() && destinationIt->second != nullptr)
				{
					const Frame& rDestinationFrame = *destinationIt->second;
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
				rReconcileContext.fPreviousHumanArmor = rRequest.data.fHealth;
			}
		}
	}

	std::swap(rReconcileContext.currentFrames, rReconcileContext.nextFrames);
	ReconcileEnsureNextFrames(rReconcileContext);

	for (auto& [rCoord, rFrameInput] : rReconcileContext.frameInputs)
	{
		rFrameInput.statusChanges.clear();
	}
}

void Game::ReconcileComputeActiveCoords(ReconcileContext& rReconcileContext)
{
	// Human's cell plus existing neighbors
	rReconcileContext.activeCoords.clear();
	if (rReconcileContext.currentFrames.contains(rReconcileContext.humanGridCoord))
	{
		rReconcileContext.activeCoords.push_back(rReconcileContext.humanGridCoord);
	}

	for (const engine::GridCoord& rOffset : engine::kNeighborOffsets)
	{
		engine::GridCoord neighbor {rReconcileContext.humanGridCoord.x + rOffset.x, rReconcileContext.humanGridCoord.y + rOffset.y};
		if (rReconcileContext.currentFrames.contains(neighbor))
		{
			rReconcileContext.activeCoords.push_back(neighbor);
		}
	}

	// Origin is always active
	if (!std::ranges::contains(rReconcileContext.activeCoords, engine::kOriginCoord) && rReconcileContext.currentFrames.contains(engine::kOriginCoord))
	{
		rReconcileContext.activeCoords.push_back(engine::kOriginCoord);
	}
}

void Game::ReconcileEnsureNextFrames(ReconcileContext& rReconcileContext)
{
	for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
	{
		if (!rReconcileContext.nextFrames.contains(rCoord))
		{
			rReconcileContext.nextFrames[rCoord] = std::make_unique<Frame>();
		}
	}
}

void Game::ReconcileBuildFrameInput(ReconcileContext& rReconcileContext, [[maybe_unused]] int64_t iServerFrame, const std::unordered_map<engine::GridCoord, CoordReconcileState::CoordServerUpdate>& rCoordUpdates)
{
	rReconcileContext.frameInputs.clear();

	// Initialize frame inputs for all active coords
	for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
	{
		auto frameIt = rReconcileContext.currentFrames.find(rCoord);
		if (frameIt == rReconcileContext.currentFrames.end())
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

void Game::ReconcileInjectPendingFullState(ReconcileContext& rReconcileContext, CoordReconcileWork& rWork)
{
	rReconcileContext.currentFrames[rWork.coord] = std::make_unique<Frame>();
	std::istringstream inputStream(rWork.pendingFullState->second);
	inputStream >> *rReconcileContext.currentFrames[rWork.coord];

	rWork.pendingFullState.reset();
}

void Game::ReconcilePruneInactiveFrames(ReconcileContext& rReconcileContext)
{
	ReconcileComputeActiveCoords(rReconcileContext);
	std::erase_if(rReconcileContext.currentFrames, [&rReconcileContext](const auto& rEntry)
	{
		const auto& [rCoord, pFrame] = rEntry;
		return !std::ranges::contains(rReconcileContext.activeCoords, rCoord);
	});
}

void Game::ReconcileRollback(ReconcileContext& rReconcileContext, int64_t iMinConfirmedFrame)
{
	// Only restore coords confirmed at iMinConfirmedFrame; coords confirmed
	// at later frames are injected during replay/catch-up at their confirmed frame
	// to avoid running physics on states that already include those frames
	rReconcileContext.currentFrames.clear();
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.iConfirmedFrame <= iMinConfirmedFrame)
		{
			rReconcileContext.currentFrames[rWork.coord] = std::make_unique<Frame>();
			std::istringstream inputStream(rWork.confirmedSerializedFrame);
			inputStream >> *rReconcileContext.currentFrames[rWork.coord];
		}
	}

	rReconcileContext.humanGridCoord = rReconcileContext.confirmedHumanState.humanGridCoord;
	rReconcileContext.humanPlayerId = rReconcileContext.confirmedHumanState.humanPlayerId;
	rReconcileContext.fPreviousHumanArmor = rReconcileContext.confirmedHumanState.fPreviousHumanArmor;
	rReconcileContext.iFrameCounter = iMinConfirmedFrame;

	// Log confirmed state after deserialization
	for (const auto& [rCoord, pFrame] : rReconcileContext.currentFrames)
	{
		FILE_LOG(0, "[Rollback] coord=({},{}) pushers={} randomEngine={} fCurrentTime={:.6f}", rCoord.x, rCoord.y, pFrame->interpolate.pushers.iCount, pFrame->postRender.randomEngine.Crc(), pFrame->interpolate.fCurrentTime);
	}

	// Log spaceship state after rollback for desync diagnosis
	for (const auto& [rCoord, pFrame] : rReconcileContext.currentFrames)
	{
		int64_t iSpaceshipCount = pFrame->interpolate.pSpaceships->iCount;
		FILE_LOG(0, "[Rollback] coord=({},{}) spaceships={}", rCoord.x, rCoord.y, iSpaceshipCount);
		if (iSpaceshipCount >= 23)
		{
			for (int64_t i = std::max(int64_t(0), iSpaceshipCount - 4); i < iSpaceshipCount; ++i)
			{
				XMFLOAT4A f4Position;
				XMStoreFloat4A(&f4Position, pFrame->interpolate.pSpaceships->pVecPositions[i]);
				FILE_LOG(0, "[Rollback] coord=({},{}) spaceship[{}] pos=({:.2f},{:.2f}) health={:.2f}", rCoord.x, rCoord.y, i, f4Position.x, f4Position.y, pFrame->postRender.pSpaceships->pfHealths[i]);
			}
		}
	}

	// Read fCurrentTime from the deserialized frame at iMinConfirmedFrame
	// (confirmedHumanState.fCurrentTime may not match iMinConfirmedFrame
	//  when coords were confirmed at different frame numbers)
	for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.iConfirmedFrame == iMinConfirmedFrame)
		{
			rReconcileContext.fCurrentTime = rReconcileContext.currentFrames[rWork.coord]->interpolate.fCurrentTime;
			break;
		}
	}

	// Inject pending full states at or before rollback frame
	for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
	{
		if (rWork.pendingFullState.has_value() && rWork.pendingFullState->first <= iMinConfirmedFrame)
		{
			ReconcileInjectPendingFullState(rReconcileContext, rWork);
		}
	}

	// Ensure next frames exist
	for (const auto& [rCoord, pFrame] : rReconcileContext.currentFrames)
	{
		if (!rReconcileContext.nextFrames.contains(rCoord))
		{
			rReconcileContext.nextFrames[rCoord] = std::make_unique<Frame>();
		}
	}
}

int64_t Game::ReconcileFindReplayRange(ReconcileContext& rReconcileContext, int64_t iMinConfirmedFrame)
{
	int64_t iReplayStart = iMinConfirmedFrame + 1;
	int64_t iMaxConsecutive = iReplayStart - 1;
	for (int64_t iFrame = iReplayStart; ; ++iFrame)
	{
		bool bAnyCoordHasData = false;
		for (const CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.serverUpdates.contains(iFrame))
			{
				bAnyCoordHasData = true;
				break;
			}
		}
		if (!bAnyCoordHasData)
		{
			break;
		}
		iMaxConsecutive = iFrame;
	}
	return iMaxConsecutive;
}

void Game::ReconcileReplay(ReconcileContext& rReconcileContext, int64_t iMinConfirmedFrame, int64_t iMaxConsecutive, const std::unordered_map<engine::GridCoord, size_t>& rCoordWorkIndex)
{
	const int64_t iReplayStart = iMinConfirmedFrame + 1;
	const int64_t iMaxReplay = (iMaxConsecutive - iMinConfirmedFrame + 1) / 2;
	int64_t iReplayCount = 0;
	std::unordered_set<engine::GridCoord> gapCoords;

	for (int64_t iFrame = iReplayStart; iFrame <= iMaxConsecutive; ++iFrame)
	{
		if (iReplayCount >= iMaxReplay || rReconcileContext.iFrameCounter >= rReconcileContext.iTargetFrame)
		{
			break;
		}

		ReconcileComputeActiveCoords(rReconcileContext);
		ReconcileEnsureNextFrames(rReconcileContext);

		++rReconcileContext.iFrameCounter;
		rReconcileContext.fCurrentTime += kfDeltaTime;

		// Gather per-coord server updates for this frame
		std::unordered_map<engine::GridCoord, CoordReconcileState::CoordServerUpdate> frameCoordUpdates;
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			auto updateIt = rWork.serverUpdates.find(iFrame);
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
			if (iFrame > rWork.iConfirmedFrame && rReconcileContext.currentFrames.contains(rWork.coord) && !frameCoordUpdates.contains(rWork.coord))
			{
				gapCoords.insert(rWork.coord);
			}
		}

		ReconcileBuildFrameInput(rReconcileContext, iFrame, frameCoordUpdates);

		for (const auto& [rCoord, rFrameInput] : rReconcileContext.frameInputs)
		{
			if (!rFrameInput.statusChanges.empty())
			{
				FILE_LOG(0, "[Reconcile] FrameInput coord=({},{}) frame={} statusChanges={}", rCoord.x, rCoord.y, iFrame, rFrameInput.statusChanges.size());
				for (size_t i = 0; i < rFrameInput.statusChanges.size(); ++i)
				{
					FILE_LOG(0, "[Reconcile]   statusChange[{}] type={} seq={}", i, static_cast<int>(rFrameInput.statusChanges.at(i).eType), rFrameInput.statusChanges.at(i).uiSequence);
				}
			}
		}

		ReconcileRunPhysics(rReconcileContext);

		// Inject pending full states at the matching transfer frame
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.pendingFullState.has_value() && rWork.pendingFullState->first == iFrame)
			{
				ReconcileInjectPendingFullState(rReconcileContext, rWork);
			}
		}

		// Inject late-confirmed coords at their confirmed frame
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.iConfirmedFrame == iFrame && rWork.iConfirmedFrame > iMinConfirmedFrame)
			{
				rReconcileContext.currentFrames[rWork.coord] = std::make_unique<Frame>();
				std::istringstream inputStream(rWork.confirmedSerializedFrame);
				inputStream >> *rReconcileContext.currentFrames[rWork.coord];

				Frame& rInjected = *rReconcileContext.currentFrames[rWork.coord];
				rInjected.interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
				common::crc_t injectedCrc = rInjected.ServerCrc();
				FILE_LOG(0, "[Reconcile] LateInject coord=({},{}) frame={} crc={} players={} fCurrentTime={}", rWork.coord.x, rWork.coord.y, iFrame, injectedCrc, rInjected.interpolate.pPlayers->iCount, rInjected.interpolate.fCurrentTime);
				if (rInjected.interpolate.pPlayers->iCount > 0)
				{
					FILE_LOG(0, "[Reconcile] LateInject player[0] pos=({},{},{}) dir=({},{},{})", rInjected.interpolate.pPlayers->pVecPositions[0].m128_f32[0], rInjected.interpolate.pPlayers->pVecPositions[0].m128_f32[1], rInjected.interpolate.pPlayers->pVecPositions[0].m128_f32[2], rInjected.interpolate.pPlayers->pVecDirections[0].m128_f32[0], rInjected.interpolate.pPlayers->pVecDirections[0].m128_f32[1], rInjected.interpolate.pPlayers->pVecDirections[0].m128_f32[2]);
				}
			}
		}

		// CRC validation per coord that had server data
		for (const auto& [rCoord, rUpdate] : frameCoordUpdates)
		{
			if (!rReconcileContext.currentFrames.contains(rCoord))
			{
				continue;
			}

			if (gapCoords.contains(rCoord))
			{
				continue;
			}

			rReconcileContext.currentFrames.at(rCoord)->interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
			common::crc_t clientCrc = rReconcileContext.currentFrames.at(rCoord)->ServerCrc();
			if (clientCrc != rUpdate.serverCrc)
			{
				common::Log("Reconcile desync at ({},{}): server={} client={} frame={}", rCoord.x, rCoord.y, rUpdate.serverCrc, clientCrc, iFrame);
				FILE_LOG(0, "[Reconcile] Desync at ({},{}): server={} client={} frame={}", rCoord.x, rCoord.y, rUpdate.serverCrc, clientCrc, iFrame);

				// Log input CRC regardless of frame CRC result
				auto inputCheckIt = rReconcileContext.frameInputs.find(rCoord);
				if (inputCheckIt != rReconcileContext.frameInputs.end())
				{
					common::crc_t clientInputCrc = inputCheckIt->second.ServerInputCrc();
					FILE_LOG(0, "[Reconcile] InputCRC at ({},{}): server={} client={} frame={} match={}", rCoord.x, rCoord.y, rUpdate.inputCrc, clientInputCrc, iFrame, clientInputCrc == rUpdate.inputCrc);
				}

				rReconcileContext.iDesyncFrame = iFrame;
				rReconcileContext.desyncCoord = rCoord;
				rReconcileContext.desyncServerCrc = rUpdate.serverCrc;
				rReconcileContext.desyncClientCrc = clientCrc;

				std::ostringstream outputStream(std::ios::binary);
				outputStream << *rReconcileContext.currentFrames.at(rCoord);
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
					common::Log("Reconcile input desync at ({},{}): server={} client={} frame={}", rCoord.x, rCoord.y, rUpdate.inputCrc, clientInputCrc, iFrame);
					FILE_LOG(0, "[Reconcile] Input desync at ({},{}): server={} client={} frame={}", rCoord.x, rCoord.y, rUpdate.inputCrc, clientInputCrc, iFrame);

					rReconcileContext.iDesyncFrame = iFrame;
					rReconcileContext.desyncCoord = rCoord;
					rReconcileContext.desyncServerCrc = rUpdate.inputCrc;
					rReconcileContext.desyncClientCrc = clientInputCrc;

					std::ostringstream outputStream(std::ios::binary);
					outputStream << *rReconcileContext.currentFrames.at(rCoord);
					std::istringstream inputStream(outputStream.str(), std::ios::binary);
					rReconcileContext.pDesyncClientFrame = std::make_unique<Frame>();
					inputStream >> *rReconcileContext.pDesyncClientFrame;
					return;
				}
			}

			// Save confirmed state per-coord at the CRC-validated frame
			auto indexIt = rCoordWorkIndex.find(rCoord);
			if (indexIt != rCoordWorkIndex.end())
			{
				CoordReconcileWork& rWork = rReconcileContext.coordWork.at(indexIt->second);
				if (!rWork.bCrcFastPath)
				{
					rWork.iNewConfirmedFrame = iFrame;
					std::ostringstream outputStream;
					outputStream << *rReconcileContext.currentFrames.at(rCoord);
					rWork.newConfirmedSerializedFrame = outputStream.str();
				}
			}
		}

		++iReplayCount;
	}
}

void Game::ReconcileCatchUp(ReconcileContext& rReconcileContext, int64_t iMinConfirmedFrame)
{
	while (rReconcileContext.iFrameCounter < rReconcileContext.iTargetFrame)
	{
		ReconcileComputeActiveCoords(rReconcileContext);
		ReconcileEnsureNextFrames(rReconcileContext);

		++rReconcileContext.iFrameCounter;
		rReconcileContext.fCurrentTime += kfDeltaTime;

		// Build empty inputs for catch-up
		rReconcileContext.frameInputs.clear();
		for (const engine::GridCoord& rCoord : rReconcileContext.activeCoords)
		{
			auto frameIt = rReconcileContext.currentFrames.find(rCoord);
			if (frameIt == rReconcileContext.currentFrames.end())
			{
				continue;
			}
			rReconcileContext.frameInputs[rCoord];
		}

		ReconcileRunPhysics(rReconcileContext);

		// Inject late-confirmed coords at their confirmed frame
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			if (rWork.iConfirmedFrame == rReconcileContext.iFrameCounter && rWork.iConfirmedFrame > iMinConfirmedFrame)
			{
				rReconcileContext.currentFrames[rWork.coord] = std::make_unique<Frame>();
				std::istringstream inputStream(rWork.confirmedSerializedFrame);
				inputStream >> *rReconcileContext.currentFrames[rWork.coord];
			}
		}

		// Store per-coord catch-up snapshots for CRC fast-path
		for (CoordReconcileWork& rWork : rReconcileContext.coordWork)
		{
			auto frameIt = rReconcileContext.currentFrames.find(rWork.coord);
			if (frameIt == rReconcileContext.currentFrames.end())
			{
				continue;
			}

			frameIt->second->interpolate.frameFlags.Clear(engine::FrameFlags::kRecalculated);
			CoordExtrapolatedSnapshot& rSnapshot = rWork.newExtrapolatedSnapshots[rReconcileContext.iFrameCounter];
			rSnapshot.crc = frameIt->second->ServerCrc();
			auto inputIt = rReconcileContext.frameInputs.find(rWork.coord);
			if (inputIt != rReconcileContext.frameInputs.end())
			{
				rSnapshot.inputCrc = inputIt->second.ServerInputCrc();
			}

			std::ostringstream outputStream;
			outputStream << *frameIt->second;
			rSnapshot.serializedFrame = outputStream.str();
		}
	}
}

#endif // BT_CLIENT

} // namespace game
