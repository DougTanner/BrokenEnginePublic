#include "GameBase.h"

#include "Game.h"
#include "Frame/FrameTick.h"
#include "Frame/HealthDamage.h"
#include "Frame/Collections/Players/Players.h"
#include "Input/Input.h"
#include "Profile/ProfileManager.h"

namespace engine
{

GameBase::GameBase()
{
	game::FrameInterpolate::Register();
}

void GameBase::PreUpdate(const game::MenuInput& rMenuInput)
{
	ProcessMenuInput(rMenuInput);
}

void GameBase::TickFrames(const game::MenuInput& rMenuInput)
{
	if (Quickload(rMenuInput)) [[unlikely]]
	{
		game::gpGame->ComputeActiveSet();
		return;
	}

	SaveLoadReplay(rMenuInput);

	// Perform full updates at fixed timestep
	int64_t iFullTicks = mTimeStep.TickRealtime();
	// Prepare active grid coordinates and per-coordinate frame inputs
	if (mpDifferenceStreamReader != nullptr)
	{
		// During replay, only the human's frame is active
		// Heap: vector clear/push_back, unordered_map insertion + make_unique<Frame>
		ScopedSuppressAllocationTracking ssat;
		game::gpGame->mActiveCoords.clear();
		game::gpGame->mActiveCoords.push_back(game::gpGame->mHumanGridCoord);
		if (mCoordFrames[game::gpGame->mHumanGridCoord].pNext == nullptr)
		{
			mCoordFrames[game::gpGame->mHumanGridCoord].pNext = std::make_unique<game::Frame>();
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
	for (int64_t i = 0; i < iFullTicks; ++i)
	{
		++miTickCounter;
		mfCurrentTime += game::kfDeltaTime;

#if defined(BT_SERVER)
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

#if defined(BT_CLIENT)
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
#if defined(BT_CLIENT)
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
				game::RunFrameTick(activeFrameRefs[j], miTickCounter, mfCurrentTime);
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

#if defined(BT_SERVER)
		// Transfer entities that crossed frame boundaries into destination frames
		if (mpDifferenceStreamReader == nullptr)
		{
			game::gpGame->HarvestTransfers();
		}
#endif

#if defined(BT_CLIENT)
		if (bExtrapolating)
		{
			game::gpGame->RecordExtrapolationSnapshot(rActiveCoords, miTickCounter);
		}
		else
#endif
		{
			for (auto& [rCoord, rFrames] : mCoordFrames)
			{
				std::swap(rFrames.pCurrent, rFrames.pNext);
			}

			// After swap, .next holds old current frames (stale data, reusable memory).
			// Ensure active entries exist for next iteration's AllocateAndCopy.
			if (mpDifferenceStreamReader == nullptr)
			{
				game::gpGame->EnsureNextFrames();
			}
			else if (mCoordFrames[game::gpGame->mHumanGridCoord].pNext == nullptr)
			{
				// Heap: make_unique<Frame> for replay target coordinate
				ScopedSuppressAllocationTracking ssat;
				mCoordFrames[game::gpGame->mHumanGridCoord].pNext = std::make_unique<game::Frame>();
			}
		}

#if defined(BT_SERVER)
		{
			// Heap: SendFullState, SendAssignPlayer, and BroadcastUpdate allocate for serialization and compression
			ScopedSuppressAllocationTracking ssat;
			game::gpGame->FinalizeNewClientsServer(miTickCounter);
			game::gpGame->DetectPlayerDeathsServer();
			game::gpGame->BroadcastStatusChangesServer(miTickCounter);
			game::gpGame->HandleSubscriptionUpdatesServer(miTickCounter);
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
		gpProfileManager->mFullUpdatesInTheLastSecond.Set(iFullTicks);
	}

	// Quicksave
	Quicksave(rMenuInput);
}

#if defined(BT_CLIENT)
void GameBase::TickFramesAndRender(const game::MenuInput& rMenuInput)
{
	TickFrames(rMenuInput);
	Render();
}

game::Frame& GameBase::RenderFrame(GridCoord coord) const
{
	if (game::gpGame->IsExtrapolating())
	{
		game::Frame* pFrame = game::gpGame->GetSnapshotFrame(coord);
		if (pFrame != nullptr)
		{
			return *pFrame;
		}
	}
	return *mCoordFrames.at(coord).pCurrent;
}

void GameBase::Render()
{
	const std::vector<GridCoord>& rActiveCoords = game::gpGame->mActiveCoords;

	// Use camera coord for rendering (human player's grid cell)
	ASSERT(mCoordFrames.contains(game::gpGame->mHumanGridCoord));
	const GridCoord cameraCoord = game::gpGame->mHumanGridCoord;

	// Interpolate elapsed time with the sub-step remainder for smooth rendering
	float fCurrentTime = mfCurrentTime + std::max(0.0f, common::NanosecondsToFloatSeconds<float>(mTimeStep.mTickRemainderNs));
	gpGraphics->RenderGlobal(RenderFrame(cameraCoord), fCurrentTime);

	// Per-frame render interpolates
	gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
	gpProfileManager->CpuStart(game::kCpuTimerFrameInterpolate);
	float fDeltaTime = std::max(0.0f, common::NanosecondsToFloatSeconds<float>(mTimeStep.mTickRemainderNs));
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
			const game::Frame& rFrame = RenderFrame(rCoord);
			game::FrameInterpolate::AllocateAndCopy(gpGraphics->mRenderInterpolates[rCoord], rFrame.interpolate);
			game::FrameInterpolate::Update(gpGraphics->mRenderInterpolates[rCoord], rFrame, fDeltaTime);
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

	gpGraphics->RenderMainPresentAcquire(iCommandBuffer, gpGraphics->mRenderInterpolates, rActiveCoords, cameraCoord);
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
				ASSERT(mCoordFrames.contains(game::gpGame->mHumanGridCoord));
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
			mCoordFrames.clear();
			auto& rSub = mCoordFrames[meta.humanGridCoord];
			rSub.pCurrent = std::make_unique<game::Frame>();
			rSub.pNext = std::make_unique<game::Frame>();

			game::gpGame->mHumanGridCoord = meta.humanGridCoord;

			// DifferenceStreamReader deserializes initial frame and initial FrameInput
			game::FrameInput initialFrameInput {};
			mpDifferenceStreamReader = std::make_unique<DifferenceStreamReader<game::Frame, game::FrameInput>>(FileFlags_t {FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7.replay"), CurrentFrame(meta.humanGridCoord), initialFrameInput);

			if (!mpDifferenceStreamReader->Loaded())
			{
				mpDifferenceStreamReader.reset();
				return;
			}

			miTickCounter = CurrentFrame(meta.humanGridCoord).interpolate.iTick;
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
			mpDifferenceStreamWriter->Update(miTickCounter, rFrameInput, rFrame);
		}
		else if (mpDifferenceStreamReader != nullptr) [[unlikely]]
		{
			if (!mpDifferenceStreamReader->LoadDifference(miTickCounter, rFrameInput))
			{
				Log("End replay {}, looping", miTickCounter);
				common::BreakOnNotEqual(rFrame, mpDifferenceStreamReader->GetSavedEnd());
				mpDifferenceStreamReader.reset();
				mGameFlags.Set(GameFlags::kLoadReplay);
			}
			else
			{
				game::gpGame->ApplyTransferStatusChanges(rFrame, rFrameInput);
				mpDifferenceStreamReader->ValidateChecksum(miTickCounter, rFrame);
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

	int64_t iFrameCount = static_cast<int64_t>(mCoordFrames.size());
	common::Write(fileStream, iFrameCount);
	humanGridCoord.Write(fileStream);

	// Sort by coord key for deterministic output
	std::vector<uint64_t> keys;
	keys.reserve(mCoordFrames.size());
	for (const auto& [rCoord, rFrames] : mCoordFrames)
	{
		keys.push_back(rCoord.ToKey());
	}
	std::sort(keys.begin(), keys.end());

	for (uint64_t uiKey : keys)
	{
		GridCoord coord = GridCoord::FromKey(uiKey);
		coord.Write(fileStream);
		fileStream << *mCoordFrames.at(coord).pCurrent;
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

	mCoordFrames.clear();

	for (int64_t i = 0; i < iFrameCount; ++i)
	{
		GridCoord coord;
		coord.Read(fileStream);
		auto pFrame = std::make_unique<game::Frame>();
		fileStream >> *pFrame;
		auto& rSub = mCoordFrames[coord];
		rSub.pCurrent = std::move(pFrame);
		rSub.pNext = std::make_unique<game::Frame>();
	}

	if (!mCoordFrames.empty())
	{
		miTickCounter = mCoordFrames.begin()->second.pCurrent->interpolate.iTick;
		mfCurrentTime = mCoordFrames.begin()->second.pCurrent->interpolate.fCurrentTime;
	}

	Log("ReadGrid {} iVersion: {} iFrameCount: {}", rFilename, iVersion, iFrameCount);
	return fileStream.good();
}

} // namespace engine
