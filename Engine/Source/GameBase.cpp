#include "GameBase.h"

#include "Multithreading.h"
#ifdef BT_CLIENT
#include "Audio/AudioManager.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/Managers/SwapchainManager.h"
#include "Graphics/Managers/TextManager.h"
#include "Input/RawInputManager.h"
#endif

#include "Game.h"
#include "Frame/Frame.h"
#include "Frame/HealthDamage.h"
#include "Input/Input.h"
#include "Profile/ProfileManager.h"

namespace engine
{

namespace
{

struct ActiveFrameRef
{
	game::Frame* pNext = nullptr;
	game::Frame* pCurrent = nullptr;
	game::FrameInput* pFrameInput = nullptr;
};

} // namespace

using enum MenuFlags;

void ResetRealTime()
{
#ifdef BT_CLIENT
	gpAudioManager->mRealTime.Reset();
	game::gpCamera->mRealTime.Reset();
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

void GameBase::UpdateFramesOnly(const game::MenuInput& rMenuInput, bool bLostFocus, bool bUpdateFrames)
{
	if (Quickload(rMenuInput)) [[unlikely]]
	{
		game::gpGame->ComputeActiveSet();
		return;
	}

	SaveLoadReplay(rMenuInput);

	// Perform full updates at fixed timestep
	int64_t iFullUpdates = mTimeStep.UpdateRealtime(bLostFocus);
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

#ifndef BT_SERVER
	if (iFullUpdates > 0 && !game::gpGame->IsNetworkMode())
	{
		// Heap: Status changes are dynamic and persistent
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		std::vector<game::StatusChange> statusChanges = game::gpGame->DrainPendingStatusChanges();
		game::gpGame->mFrameInputs.at(game::gpGame->mHumanGridCoord).statusChanges = std::move(statusChanges);
	}
#endif
	const std::vector<GridCoord>& rActiveCoords = game::gpGame->mActiveCoords;

	gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
	for (int64_t i = 0; i < iFullUpdates; ++i)
	{
		++miFrameCounter;
		mfCurrentTime += game::kfDeltaTime;

#ifndef BT_SERVER
		if (!game::gpGame->IsNetworkMode())
		{
			// Inject pending transfer StatusChanges from previous iteration's HarvestTransfers
			{
				// Heap: vector insert for transfer StatusChanges
				ScopedSuppressAllocationTracking ssat;
				std::vector<game::StatusChange> transfers = game::gpGame->DrainPendingTransferChanges();
				if (!transfers.empty())
				{
					std::vector<game::StatusChange>& rStatusChanges = game::gpGame->mFrameInputs.at(game::gpGame->mHumanGridCoord).statusChanges;
					rStatusChanges.insert(rStatusChanges.end(), transfers.begin(), transfers.end());
				}
			}

			SyncReplay(CurrentFrame(game::gpGame->mHumanGridCoord), game::gpGame->mFrameInputs.at(game::gpGame->mHumanGridCoord));
		}
#endif

		const int64_t iActiveCount = static_cast<int64_t>(rActiveCoords.size());

		// Pre-resolve frame references to avoid repeated map lookups across all phases
		common::gpThreadLocal->mWorkbuffer.Push();
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			const GridCoord& rCoord = rActiveCoords[static_cast<size_t>(j)];
			common::gpThreadLocal->mWorkbuffer.PushBack<ActiveFrameRef>({
				.pNext = &NextFrame(rCoord),
				.pCurrent = &CurrentFrame(rCoord),
				.pFrameInput = &game::gpGame->mFrameInputs.at(rCoord),
			});
		}
		std::span<const ActiveFrameRef> activeFrameRefs = common::gpThreadLocal->mWorkbuffer.Span<ActiveFrameRef>();

		gpProfileManager->CpuStart(game::kCpuTimerFrameInterpolate);
		if (iActiveCount > 1)
		{
			auto processRange = [&](int64_t iStart, int64_t iEnd)
			{
				ScopedSuppressCpuProfiling scopedSuppressCpuProfiling;
				for (int64_t j = iStart; j < iEnd; ++j)
				{
					game::Frame& rNext = *activeFrameRefs[j].pNext;
					const game::Frame& rCurrent = *activeFrameRefs[j].pCurrent;
					game::FrameInterpolate::AllocateAndCopy(rNext.interpolate, rCurrent.interpolate);
					game::FrameInterpolate::Update(rNext.interpolate, rCurrent, game::kfDeltaTime);
					rNext.interpolate.iFrame = miFrameCounter;
					rNext.interpolate.fCurrentTime = mfCurrentTime;
				}
			};
			common::gpMultithreading->Dispatch(iActiveCount, processRange);
		}
		else
		{
			game::Frame& rNext = *activeFrameRefs[0].pNext;
			const game::Frame& rCurrent = *activeFrameRefs[0].pCurrent;
			game::FrameInterpolate::AllocateAndCopy(rNext.interpolate, rCurrent.interpolate);
			game::FrameInterpolate::Update(rNext.interpolate, rCurrent, game::kfDeltaTime);
			rNext.interpolate.iFrame = miFrameCounter;
			rNext.interpolate.fCurrentTime = mfCurrentTime;
		}
		gpProfileManager->CpuStop(game::kCpuTimerFrameInterpolate, false);

		gpProfileManager->CpuStart(game::kCpuTimerFramePostRender);
		if (iActiveCount > 1)
		{
			auto processRange = [&](int64_t iStart, int64_t iEnd)
			{
				ScopedSuppressCpuProfiling scopedSuppressCpuProfiling;
				for (int64_t j = iStart; j < iEnd; ++j)
				{
					game::Frame& rNext = *activeFrameRefs[j].pNext;
					const game::Frame& rCurrent = *activeFrameRefs[j].pCurrent;
					game::FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;
					game::FramePostRender::AllocateAndCopy(rNext.postRender, rCurrent.postRender);

					// Ensure playerInputs covers current player count (may have grown via Spawn or HarvestTransfers on prior iteration)
					if (rNext.interpolate.players.iCount > static_cast<int64_t>(rFrameInput.playerInputs.size()))
					{
						// Heap: FrameInput.playerInputs must persist across the full PostRender phase; player count can grow via Spawn or HarvestTransfers between iterations
						ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
						rFrameInput.playerInputs.resize(rNext.interpolate.players.iCount);
					}

					game::FramePostRender::Update(rNext, rCurrent, rFrameInput);
				}
			};
			common::gpMultithreading->Dispatch(iActiveCount, processRange);
		}
		else
		{
			game::Frame& rNext = *activeFrameRefs[0].pNext;
			const game::Frame& rCurrent = *activeFrameRefs[0].pCurrent;
			game::FrameInput& rFrameInput = *activeFrameRefs[0].pFrameInput;
			game::FramePostRender::AllocateAndCopy(rNext.postRender, rCurrent.postRender);

			// Ensure playerInputs covers current player count (may have grown via Spawn or HarvestTransfers on prior iteration)
			if (rNext.interpolate.players.iCount > static_cast<int64_t>(rFrameInput.playerInputs.size()))
			{
				// Heap: FrameInput.playerInputs must persist across the full PostRender phase; player count can grow via Spawn or HarvestTransfers between iterations
				ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
				rFrameInput.playerInputs.resize(rNext.interpolate.players.iCount);
			}

			game::FramePostRender::Update(rNext, rCurrent, rFrameInput);
		}

		// Collision uses static storage -- must run as atomic block per Frame
		for (int64_t j = 0; j < iActiveCount; ++j)
		{
			game::Frame& rNext = *activeFrameRefs[j].pNext;
			const game::Frame& rCurrent = *activeFrameRefs[j].pCurrent;
			game::FramePostRender::PreCollision(rNext, rCurrent);
			Collision::Collide(rNext.postRender.alignments, rNext.postRender.vecArea);
			game::FramePostRender::PostCollision(rNext, rCurrent);
			game::FramePostRender::AreaDamage(rNext, rCurrent);
		}

		// Transfer entities that reached frame boundaries
		if (iActiveCount > 1)
		{
			auto processRange = [&](int64_t iStart, int64_t iEnd)
			{
				ScopedSuppressCpuProfiling scopedSuppressCpuProfiling;
				for (int64_t j = iStart; j < iEnd; ++j)
				{
					game::FramePostRender::Transfer(*activeFrameRefs[j].pNext);
				}
			};
			common::gpMultithreading->Dispatch(iActiveCount, processRange);
		}
		else
		{
			game::FramePostRender::Transfer(*activeFrameRefs[0].pNext);
		}

		// Destroy expired entities and spawn new ones
		if (iActiveCount > 1)
		{
			auto processRange = [&](int64_t iStart, int64_t iEnd)
			{
				ScopedSuppressCpuProfiling scopedSuppressCpuProfiling;
				for (int64_t j = iStart; j < iEnd; ++j)
				{
					game::Frame& rNext = *activeFrameRefs[j].pNext;
					game::FrameInput& rFrameInput = *activeFrameRefs[j].pFrameInput;
					game::FramePostRender::Destroy(rNext);
					game::FramePostRender::Spawn(rNext, rFrameInput);
				}
			};
			common::gpMultithreading->Dispatch(iActiveCount, processRange);
		}
		else
		{
			game::Frame& rNext = *activeFrameRefs[0].pNext;
			game::FrameInput& rFrameInput = *activeFrameRefs[0].pFrameInput;
			game::FramePostRender::Destroy(rNext);
			game::FramePostRender::Spawn(rNext, rFrameInput);
		}

		gpProfileManager->CpuStop(game::kCpuTimerFramePostRender, false);

		common::gpThreadLocal->mWorkbuffer.Pop();

		// Transfer entities that crossed frame boundaries into destination frames
		if (mpDifferenceStreamReader == nullptr)
		{
			game::gpGame->HarvestTransfers();
		}

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

		for (auto& [rCoord, rFrameInput] : game::gpGame->mFrameInputs)
		{
			rFrameInput.ClearPressed();
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
void GameBase::UpdateFramesAndRender(const game::MenuInput& rMenuInput, bool bLostFocus, bool bUpdateFrames)
{
	// Wait for previous render thread to finish reading mCurrentFrames before modifying frame maps
	gpGraphics->WaitForRender();

	UpdateFramesOnly(rMenuInput, bLostFocus, bUpdateFrames);

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

	// Launch async render with captured index
	if constexpr (kbEnableRenderThread)
	{
		gpGraphics->mRenderFuture.Wake([iCommandBuffer, &rActiveCoords, cameraCoord, this]()
		{
			gpGraphics->RenderMainPresentAcquire(iCommandBuffer, gpGraphics->mRenderInterpolates, rActiveCoords, cameraCoord, mCurrentFrames);
		});
	}
	else
	{
		gpGraphics->RenderMainPresentAcquire(iCommandBuffer, gpGraphics->mRenderInterpolates, rActiveCoords, cameraCoord, mCurrentFrames);
	}
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
