#include "GameBase.h"

#include "Game.h"
#include "Frame/FrameTick.h"
#include "Frame/HealthDamage.h"
#include "Frame/Collections/Players/Players.h"
#include "Input/Input.h"
#include "Profile/ProfileManager.h"

namespace engine
{

using enum MenuFlags;

void ResetRealTime()
{
#ifdef BT_CLIENT
	gpAudioManager->mRealTime.Reset();
	gpGraphics->mRenderFrameTimer.Reset();
#endif
	game::gpGame->mTimeStep.Reset();
}

GameBase::GameBase()
{
	game::FrameInterpolate::Register();
}

bool GameBase::PreUpdate(const game::MenuInput& rMenuInput, bool bLostFocus)
{
	ProcessMenuInput(rMenuInput);

	// Reset timers when update state changes (pause/unpause transitions or focus loss)
	bool bUpdateFrame = ShouldUpdateFrame();
	if (bLostFocus || !bUpdateFrame || bUpdateFrame != static_cast<bool>(mGameFlags & GameFlags::kPreviousFrameUpdated)) [[unlikely]]
	{
#ifdef BT_CLIENT
		gpRawInputManager->SetVibration(0, 0.0f, 0.0f);
#endif
		ResetRealTime();
	}
	if (bUpdateFrame)
	{
		mGameFlags.Set(GameFlags::kPreviousFrameUpdated);
	}
	else
	{
		mGameFlags.Clear(GameFlags::kPreviousFrameUpdated);
	}

	return bUpdateFrame;
}

void GameBase::UpdateFrames(const game::MenuInput& rMenuInput, bool bUpdateFrames)
{
	if (Quickload(rMenuInput)) [[unlikely]]
	{
		game::gpGame->ComputeActiveSet();
		return;
	}

	SaveLoadReplay(rMenuInput);

	// Perform full updates at fixed timestep
	int64_t iFullUpdates = mTimeStep.UpdateRealtime();
	if (!bUpdateFrames)
	{
		iFullUpdates = 0;
	}

	// Prepare active grid coordinates and per-coordinate frame inputs
	if (mpDifferenceStreamReader != nullptr)
	{
		// During replay, only the human's frame is active
		// Heap: vector clear/push_back, unordered_map insertion + make_unique<Frame>
		ScopedSuppressAllocationTracking ssat;
		game::gpGame->mActiveCoords.clear();
		game::gpGame->mActiveCoords.push_back(game::gpGame->mHumanGridCoord);
		if (!mNextFrames.contains(game::gpGame->mHumanGridCoord))
		{
			mNextFrames[game::gpGame->mHumanGridCoord] = std::make_unique<game::Frame>();
		}
		game::gpGame->BuildFrameInputs();
	}
	else
	{
		game::gpGame->ComputeActiveSet();
		game::gpGame->EnsureNextFrames();
		game::gpGame->BuildFrameInputs();
	}

	const std::vector<GridCoord>& rActiveCoords = game::gpGame->mActiveCoords;

	gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
	for (int64_t i = 0; i < iFullUpdates; ++i)
	{
		++miFrameCounter;
		mfCurrentTime += game::kfDeltaTime;

#ifdef BT_SERVER
		// Recompute active set each tick so new client subscriptions
		// (set by FinalizeNewClientsServer on the previous frame) are picked up immediately
		{
			ScopedSuppressAllocationTracking ssat;
			game::gpGame->ComputeActiveSetServer();
			game::gpGame->EnsureNextFrames();

			// Add empty frame inputs for any newly active coords
			for (const GridCoord& rCoord : game::gpGame->mActiveCoords)
			{
				if (!game::gpGame->mFrameInputs.contains(rCoord))
				{
					game::gpGame->mFrameInputs[rCoord];
				}
			}
		}
#endif

		const int64_t iActiveCount = static_cast<int64_t>(rActiveCoords.size());

#ifdef BT_CLIENT
		bool bExtrapolating = game::gpGame->IsExtrapolating();
		if (bExtrapolating)
		{
			game::gpGame->PrepareExtrapolationTick(rActiveCoords);
		}
#endif

		// Pre-resolve frame references to avoid repeated map lookups across all phases
		common::gpThreadLocal->mWorkbuffer.Push();
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			const GridCoord& rCoord = rActiveCoords[static_cast<size_t>(j)];
#ifdef BT_CLIENT
			if (bExtrapolating)
			{
				game::Frame* pNext = nullptr;
				game::Frame* pCurrent = nullptr;
				game::gpGame->BuildExtrapolationFrameRef(rCoord, pNext, pCurrent);
				if (pNext != nullptr)
				{
					common::gpThreadLocal->mWorkbuffer.PushBack<game::ActiveFrameRef>({
						.pNext = pNext,
						.pCurrent = pCurrent,
						.pFrameInput = &game::gpGame->mFrameInputs.at(rCoord),
					});
					continue;
				}
			}
#endif
			common::gpThreadLocal->mWorkbuffer.PushBack<game::ActiveFrameRef>({
				.pNext = &NextFrame(rCoord),
				.pCurrent = &CurrentFrame(rCoord),
				.pFrameInput = &game::gpGame->mFrameInputs.at(rCoord),
			});
		}
		std::span<const game::ActiveFrameRef> activeFrameRefs = common::gpThreadLocal->mWorkbuffer.Span<game::ActiveFrameRef>();

		gpProfileManager->CpuStart(game::kCpuTimerFrameInterpolate);
		gpProfileManager->CpuStart(game::kCpuTimerFramePostRender);

		auto processRange = [&](int64_t iBegin, int64_t iEnd)
		{
			for (int64_t j = iBegin; j < iEnd; ++j)
			{
				game::RunFrameTick(activeFrameRefs[j], miFrameCounter, mfCurrentTime);
			}
		};
		if constexpr (kbEnableFrameDispatch)
		{
			common::gpMultithreading->Dispatch(iActiveCount, processRange);
		}
		else
		{
			processRange(0, iActiveCount);
		}

		gpProfileManager->CpuStop(game::kCpuTimerFramePostRender, false);
		gpProfileManager->CpuStop(game::kCpuTimerFrameInterpolate, false);

		common::gpThreadLocal->mWorkbuffer.Pop();

#ifdef BT_SERVER
		// Transfer entities that crossed frame boundaries into destination frames
		if (mpDifferenceStreamReader == nullptr)
		{
			game::gpGame->HarvestTransfers();
		}
#endif

#ifdef BT_CLIENT
		if (bExtrapolating)
		{
			game::gpGame->RecordExtrapolationSnapshot(rActiveCoords, miFrameCounter);
		}
		else
#endif
		{
			std::swap(mCurrentFrames, mNextFrames);

			// After swap, mNextFrames holds old current frames (stale data, reusable memory).
			// Ensure active entries exist for next iteration's AllocateAndCopy.
			if (mpDifferenceStreamReader == nullptr)
			{
				game::gpGame->EnsureNextFrames();
			}
			else if (!mNextFrames.contains(game::gpGame->mHumanGridCoord))
			{
				// Heap: make_unique<Frame> for replay target coordinate
				ScopedSuppressAllocationTracking ssat;
				mNextFrames[game::gpGame->mHumanGridCoord] = std::make_unique<game::Frame>();
			}
		}

#ifdef BT_SERVER
		{
			// Heap: SendFullState, SendAssignPlayer, and BroadcastUpdate allocate for serialization and compression
			ScopedSuppressAllocationTracking ssat;
			game::gpGame->FinalizeNewClientsServer(miFrameCounter);
			game::gpGame->DetectPlayerDeathsServer();
			game::gpGame->BroadcastStatusChangesServer(miFrameCounter);
			game::gpGame->HandleSubscriptionUpdatesServer(miFrameCounter);
			engine::gpNetworkServer->Flush();
		}
#endif

		for (auto& [rCoord, rFrameInput] : game::gpGame->mFrameInputs)
		{
			rFrameInput.statusChanges.clear();
		}
	}
	gpProfileManager->CpuStop(game::kCpuTimerFrameUpdate, false);

	if constexpr (kbEnableProfiling)
	{
		gpProfileManager->mFullUpdatesInTheLastSecond.Set(iFullUpdates);
	}

	// Quicksave
	Quicksave(rMenuInput);
}

#ifdef BT_CLIENT
void GameBase::UpdateFramesAndRender(const game::MenuInput& rMenuInput, bool bUpdateFrames)
{
	UpdateFrames(rMenuInput, bUpdateFrames);
	BorrowSnapshotFramesForRender();
	Render(bUpdateFrames);
	RestoreSnapshotFramesAfterRender();
}

void GameBase::BorrowSnapshotFramesForRender()
{
	if (!game::gpGame->IsExtrapolating())
	{
		return;
	}

	game::gpGame->BorrowSnapshotFrames(mCurrentFrames);
}

void GameBase::RestoreSnapshotFramesAfterRender()
{
	if (!game::gpGame->IsExtrapolating())
	{
		return;
	}

	game::gpGame->RestoreSnapshotFrames(mCurrentFrames);
}

void GameBase::Render(bool bUpdateFrames)
{
	const std::vector<GridCoord>& rActiveCoords = game::gpGame->mActiveCoords;

	// Use camera coord for rendering (human player's grid cell)
	ASSERT(mCurrentFrames.contains(game::gpGame->mHumanGridCoord));
	const GridCoord cameraCoord = game::gpGame->mHumanGridCoord;

	// Interpolate elapsed time with the sub-step remainder for smooth rendering
	float fCurrentTime = mfCurrentTime + (bUpdateFrames ? common::NanosecondsToFloatSeconds<float>(mTimeStep.mUpdateRemainderNs) : 0.0f);
	gpGraphics->RenderGlobal(CurrentFrame(cameraCoord), fCurrentTime);

	// Per-frame render interpolates
	gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
	gpProfileManager->CpuStart(game::kCpuTimerFrameInterpolate);
	float fDeltaTime = bUpdateFrames ? common::NanosecondsToFloatSeconds<float>(mTimeStep.mUpdateRemainderNs) : 0.0f;
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

		// Heap: std::erase_if may rehash, operator[] may insert — suppressed like the old MergeFramesForRender
		// Remove render interpolates for deactivated coords
		std::erase_if(gpGraphics->mRenderInterpolates, [&](const std::pair<const GridCoord, game::FrameInterpolate>& rPair)
		{
			return !std::ranges::contains(rActiveCoords, rPair.first);
		});

		// AllocateAndCopy + Update each active frame's render interpolate (camera frame first)
		auto interpolateFrame = [&](const GridCoord& rCoord)
		{
			game::FrameInterpolate::AllocateAndCopy(gpGraphics->mRenderInterpolates[rCoord], CurrentFrame(rCoord).interpolate);
			game::FrameInterpolate::Update(gpGraphics->mRenderInterpolates[rCoord], CurrentFrame(rCoord), fDeltaTime);
		};
		interpolateFrame(cameraCoord);
		for (const GridCoord& rCoord : rActiveCoords)
		{
			if (rCoord != cameraCoord)
			{
				interpolateFrame(rCoord);
			}
		}
	}
	gpProfileManager->CpuStop(game::kCpuTimerFrameInterpolate, false);
	gpProfileManager->CpuStop(game::kCpuTimerFrameUpdate, false);

	if constexpr (kbEnableProfiling)
	{
		gpProfileManager->mInterpolateUpdatesInTheLastSecond.Set();
	}

	// Update camera before async launch
	game::gpCamera->Update(gpGraphics->mRenderInterpolates.at(cameraCoord));

	// Write UI buffers on main thread (safe - Update() already complete)
	// Capture command buffer index before async launch to avoid re-reading in async thread
	int64_t iCommandBuffer = gpSwapchainManager->miFramebufferIndex;
	gpTextManager->RenderMain(iCommandBuffer);

	gpGraphics->RenderMainPresentAcquire(iCommandBuffer, gpGraphics->mRenderInterpolates, rActiveCoords, cameraCoord, mCurrentFrames);
}
#endif

void GameBase::Quicksave([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	// Heap: fstream and Frame serialization (stream must stay open across the full write so push/pop
	//   lifecycle doesn't apply, and SOA collection data must persist in the Frame after deserialization)
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbEnableDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kQuicksave)
		{
			WriteGrid({FileFlags::kAppDataDirectory, FileFlags::kWrite}, QuicksaveFile(), game::gpGame->mHumanGridCoord);
		}
	}
}

bool GameBase::Quickload([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	// Heap: fstream and Frame deserialization allocate vectors for variable-size SOA collections.
	//   Stream must stay open across the read, and collection data must persist in the Frame afterward
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbEnableDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kQuickload || rMenuInput.flags & game::MenuInputFlags::kResetFrame)
		{
			GridCoord loadedHumanGridCoord {};
			bool bQuickloaded = false;

			if (rMenuInput.flags & game::MenuInputFlags::kQuickload)
			{
				if (!ReadGrid({FileFlags::kAppDataDirectory, FileFlags::kRead}, QuicksaveFile(), loadedHumanGridCoord))
				{
					game::gpGame->CreateNewFrame(game::GameFlags::kGame);
				}
				else
				{
					bQuickloaded = true;
				}
			}
			else
			{
				game::gpGame->CreateNewFrame(game::GameFlags::kGame);
			}

			Reset();

			if (bQuickloaded)
			{
				game::gpGame->mHumanGridCoord = loadedHumanGridCoord;
				ASSERT(mCurrentFrames.contains(game::gpGame->mHumanGridCoord));
			}

			return true;
		}
	}

	return false;
}

void GameBase::SaveLoadReplay([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	if constexpr (kbEnableDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kSaveReplay)
		{
			mGameFlags.Set(GameFlags::kSaveReplay);
		}
		else if (rMenuInput.flags & game::MenuInputFlags::kLoadReplay)
		{
			mGameFlags.Set(GameFlags::kLoadReplay);
		}

		if (mGameFlags & GameFlags::kLoadReplay)
		{
			// Heap: DifferenceStream reader + Frame deserialization + ReplayMeta file I/O
			ScopedSuppressAllocationTracking suppressAllocationTracking;

			mGameFlags.Clear(GameFlags::kLoadReplay);

			if (mpDifferenceStreamReader != nullptr)
			{
				mpDifferenceStreamReader.reset();
				return;
			}

			game::ReplayMeta meta {};
			if (!ReadVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7.replay.meta"), meta))
			{
				Log("Failed to read replay metadata");
				return;
			}

			Reset();

			// Clear all frames and create fresh at recorded coordinate
			mCurrentFrames.clear();
			mNextFrames.clear();
			mCurrentFrames[meta.humanGridCoord] = std::make_unique<game::Frame>();
			mNextFrames[meta.humanGridCoord] = std::make_unique<game::Frame>();

			game::gpGame->mHumanGridCoord = meta.humanGridCoord;

			// DifferenceStreamReader deserializes initial frame and initial FrameInput
			game::FrameInput initialFrameInput {};
			mpDifferenceStreamReader = std::make_unique<DifferenceStreamReader<game::Frame, game::FrameInput>>(FileFlags_t {FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7.replay"), CurrentFrame(meta.humanGridCoord), initialFrameInput);

			if (!mpDifferenceStreamReader->Loaded())
			{
				mpDifferenceStreamReader.reset();
				return;
			}

			miFrameCounter = CurrentFrame(meta.humanGridCoord).interpolate.iFrame;
			mfCurrentTime = CurrentFrame(meta.humanGridCoord).interpolate.fCurrentTime;
			game::gpGame->RestoreReplayMeta(meta);
		}
	}
}

void GameBase::SyncReplay([[maybe_unused]] game::Frame& rFrame, [[maybe_unused]] game::FrameInput& rFrameInput)
{
	// Heap: DifferenceStream reader/writer persist across frames, growing vectors for diffs and checksums.
	//   Workbuffer is popped each frame so can't hold cross-frame state; size depends on recording length
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbEnableDebugInput)
	{
		if ((mGameFlags & GameFlags::kSaveReplay) && mpDifferenceStreamWriter == nullptr)
		{
			mGameFlags.Clear(GameFlags::kSaveReplay);
			mpDifferenceStreamReader.reset();
			mpDifferenceStreamWriter = std::make_unique<DifferenceStreamWriter<game::Frame, game::FrameInput>>(rFrame, rFrameInput);
			return;
		}
		else if ((mGameFlags & GameFlags::kSaveReplay) && mpDifferenceStreamWriter != nullptr)
		{
			mGameFlags.Clear(GameFlags::kSaveReplay);
			mpDifferenceStreamWriter->Save({FileFlags::kAppDataDirectory, FileFlags::kWrite, FileFlags::kBackup}, std::filesystem::path("F7.replay"), rFrame);
			mpDifferenceStreamWriter.reset();

			// Write replay metadata for F8 load
			game::ReplayMeta meta {
				.humanGridCoord = game::gpGame->mHumanGridCoord,
				.iHumanPlayerIdValue = game::gpGame->HumanPlayerId().ToUuid().Value(),
				.fPreviousHumanArmor = game::gpGame->PreviousHumanArmor(),
			};
			WriteVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kWrite}, std::filesystem::path("F7.replay.meta"), meta);

			return;
		}

		if (mpDifferenceStreamWriter != nullptr) [[unlikely]]
		{
			mpDifferenceStreamWriter->Update(miFrameCounter, rFrameInput, rFrame);
		}
		else if (mpDifferenceStreamReader != nullptr) [[unlikely]]
		{
			if (!mpDifferenceStreamReader->LoadDifference(miFrameCounter, rFrameInput))
			{
				Log("End replay {}, looping", miFrameCounter);
				common::BreakOnNotEqual(rFrame, mpDifferenceStreamReader->GetSavedEnd());
				mpDifferenceStreamReader.reset();
				mGameFlags.Set(GameFlags::kLoadReplay);
			}
			else
			{
				game::gpGame->ApplyTransferStatusChanges(rFrame, rFrameInput);
				mpDifferenceStreamReader->ValidateChecksum(miFrameCounter, rFrame);
			}
		}
	}
}

void GameBase::WriteGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord humanGridCoord)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);
	int64_t iVersion = game::Frame::kiVersion;
	common::Write(fileStream, iVersion);
	int64_t iSize = 0;
	common::Write(fileStream, iSize);

	int64_t iFrameCount = static_cast<int64_t>(mCurrentFrames.size());
	common::Write(fileStream, iFrameCount);
	humanGridCoord.Write(fileStream);

	// Sort by coord key for deterministic output
	std::vector<uint64_t> keys;
	keys.reserve(mCurrentFrames.size());
	for (const auto& [rCoord, pFrame] : mCurrentFrames)
	{
		keys.push_back(rCoord.ToKey());
	}
	std::sort(keys.begin(), keys.end());

	for (uint64_t uiKey : keys)
	{
		GridCoord coord = GridCoord::FromKey(uiKey);
		coord.Write(fileStream);
		fileStream << *mCurrentFrames.at(coord);
	}

	Log("WriteGrid {} iVersion: {} iFrameCount: {}", rFilename, iVersion, iFrameCount);
}

bool GameBase::ReadGrid(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, GridCoord& rHumanGridCoord)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);

	int64_t iVersion = 0;
	common::Read(fileStream, iVersion);
	int64_t iSize = 0;
	common::Read(fileStream, iSize);

	if (iVersion != game::Frame::kiVersion)
	{
		Log("ReadGrid {} failed: version {} != {}", rFilename, iVersion, game::Frame::kiVersion);
		return false;
	}

	int64_t iFrameCount = 0;
	common::Read(fileStream, iFrameCount);
	rHumanGridCoord.Read(fileStream);

	mCurrentFrames.clear();
	mNextFrames.clear();

	for (int64_t i = 0; i < iFrameCount; ++i)
	{
		GridCoord coord;
		coord.Read(fileStream);
		auto pFrame = std::make_unique<game::Frame>();
		fileStream >> *pFrame;
		mCurrentFrames[coord] = std::move(pFrame);
	}

	// Create empty next frames for all loaded coords
	for (const auto& [rCoord, pFrame] : mCurrentFrames)
	{
		mNextFrames[rCoord] = std::make_unique<game::Frame>();
	}

	if (!mCurrentFrames.empty())
	{
		miFrameCounter = mCurrentFrames.begin()->second->interpolate.iFrame;
		mfCurrentTime = mCurrentFrames.begin()->second->interpolate.fCurrentTime;
	}

	Log("ReadGrid {} iVersion: {} iFrameCount: {}", rFilename, iVersion, iFrameCount);
	return fileStream.good();
}

} // namespace engine
