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
	: mGameSaveLoad(*this)
{
	game::FrameInterpolate::Register();

#if defined(BT_SERVER)
	timeBeginPeriod(1);
	mTimerHandle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
#endif
}

GameBase::~GameBase()
{
#if defined(BT_SERVER)
	CloseHandle(mTimerHandle);
	timeEndPeriod(1);
#endif
}

void GameBase::ProcessInput([[maybe_unused]] bool bLostFocus, game::MenuInput& rMenuInput)
{
#if defined(BT_CLIENT)
	game::gpInput->UpdateMenuInput(bLostFocus, rMenuInput);
#endif
	ProcessMenuInput(rMenuInput);
}

void GameBase::TickFrames(const game::MenuInput& rMenuInput)
{
#if defined(BT_CLIENT)
	game::gpGame->PollAndReconcileClient();

	if (game::gpGame->GetDesyncTick() >= 0)
		return;
#endif

#if defined(BT_SERVER)
	game::gpGame->PreTickNetworkServer();
#endif

	if (mGameSaveLoad.Quickload(rMenuInput)) [[unlikely]]
	{
		game::gpGame->ComputeActiveSet();
		return;
	}

	mGameSaveLoad.SaveLoadReplay(rMenuInput);

#if defined(BT_SERVER)
	WaitForServerTick();
#endif

	int64_t iFullTicks = mTimeStep.TickRealtime();
#if defined(BT_SERVER)
	if (iFullTicks != 1) [[unlikely]]
	{
		Log("iFullTicks: {} != 1", iFullTicks);
	}
#endif
	if (iFullTicks > 0) common::Log("GameBase: TickFrames ticks={} tickCounter={}", iFullTicks, miTickCounter); // DT: TEMP
	PrepareActiveSet();

	const std::vector<GridCoord>& rActiveCoords = game::gpGame->mActiveCoords;

	gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
	for (int64_t i = 0; i < iFullTicks; ++i)
	{
		++miTickCounter;
		mfCurrentTime += game::kfDeltaTime;

#if defined(BT_SERVER)
		PrepareServerTick();
#endif

		bool bExtrapolating = false;
#if defined(BT_CLIENT)
		bExtrapolating = game::gpGame->IsExtrapolating();
		if (bExtrapolating)
			game::gpGame->PrepareExtrapolationTick(rActiveCoords);
		common::Log("GameBase: Tick {} extrapolating={} activeCoords={}", miTickCounter, bExtrapolating, static_cast<int64_t>(rActiveCoords.size())); // DT: TEMP
#endif

		BuildAndDispatchFrameTicks(rActiveCoords, bExtrapolating);
		FinalizeFrameTick(rActiveCoords, bExtrapolating);
	}
	gpProfileManager->CpuStop(game::kCpuTimerFrameUpdate, false);

	if constexpr (kbEnableProfiling)
		gpProfileManager->mFullUpdatesInTheLastSecond.Set(iFullTicks);

	mGameSaveLoad.Quicksave(rMenuInput);

#if defined(BT_CLIENT)
	game::gpGame->PostTickNetworkClient();
#endif
}

#if defined(BT_SERVER)
void GameBase::PrepareServerTick()
{
	// Recompute active set each tick so new client subscriptions
	// (set by FinalizeNewClientsServer on the previous frame) are picked up immediately
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

void GameBase::BuildAndDispatchFrameTicks(const std::vector<GridCoord>& rActiveCoords, [[maybe_unused]] bool bExtrapolating)
{
	const int64_t iActiveCount = static_cast<int64_t>(rActiveCoords.size());

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
}

void GameBase::FinalizeFrameTick([[maybe_unused]] const std::vector<GridCoord>& rActiveCoords, [[maybe_unused]] bool bExtrapolating)
{
#if defined(BT_SERVER)
	// Transfer entities that crossed frame boundaries into destination frames
	if (!mGameSaveLoad.IsReplaying())
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
		SwapFrames();
	}

#if defined(BT_SERVER)
	BroadcastServerTick();
#endif

	for (auto& [rCoord, rFrameInput] : game::gpGame->mFrameInputs)
	{
		rFrameInput.statusChanges.clear();
	}
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

	game::gpGame->PostRenderNetworkClient();
}
#endif

#if defined(BT_SERVER)
void GameBase::WaitForServerTick()
{
	// Server sleeps until next tick (there is no VSync wait), hybrid approach: waitable timer for the bulk, then spin-wait for precision
	constexpr std::chrono::nanoseconds kSpinMarginNs = 2000000ns;
	std::chrono::nanoseconds remainingNs = game::kTickNs - mTimeStep.mTickRemainderNs - mTimeStep.mRealTime.GetDeltaNs();
	std::chrono::nanoseconds sleepNs = remainingNs - kSpinMarginNs;
	if (sleepNs > 0ns)
	{
		LARGE_INTEGER dueTime {.QuadPart = -(sleepNs.count() / 100)}; // Negative = relative, 100ns units
		SetWaitableTimerEx(mTimerHandle, &dueTime, 0, nullptr, nullptr, nullptr, 0);
		WaitForSingleObject(mTimerHandle, INFINITE);
	}

	// Spin-wait
	while (mTimeStep.mRealTime.GetDeltaNs() + mTimeStep.mTickRemainderNs < game::kTickNs)
	{
	}

	// Verify precision
	constexpr std::chrono::nanoseconds kTickMarginNs = game::kTickNs / 64;
	std::chrono::nanoseconds remainderNs = mTimeStep.mRealTime.GetDeltaNs() + mTimeStep.mTickRemainderNs - game::kTickNs;
	static int64_t siTotalTicks = 0;
	static int64_t siOvershootTicks = 0;
	++siTotalTicks;
	if ((remainderNs < 0ns || remainderNs > kTickMarginNs)) [[unlikely]]
	{
		++siOvershootTicks;
		Log("Sleep/busy wait precision: remainderNs={} ({}/{}={}%)", remainderNs.count(), siOvershootTicks, siTotalTicks, siOvershootTicks * 100 / siTotalTicks);
	}
}

void GameBase::BroadcastServerTick()
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

void GameBase::PrepareActiveSet()
{
	if (mGameSaveLoad.IsReplaying())
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
}

void GameBase::SwapFrames()
{
	for (auto& [rCoord, rFrames] : mCoordFrames)
	{
		std::swap(rFrames.pCurrent, rFrames.pNext);
	}

	// After swap, .next holds old current frames (stale data, reusable memory).
	// Ensure active entries exist for next iteration's AllocateAndCopy.
	if (!mGameSaveLoad.IsReplaying())
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

} // namespace engine
