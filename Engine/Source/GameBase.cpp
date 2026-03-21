#include "GameBase.h"

#include "Game.h"
#if defined(BT_CLIENT)
#include "Network/ClientSession.h"
#endif
#if defined(BT_SERVER)
#include "Network/ServerSession.h"
#endif
#include "Frame/FrameTick.h"
#include "Frame/HealthDamage.h"
#include "Frame/Collections/Players/Players.h"
#include "Input/Input.h"
#include "Profile/ProfileManager.h"

namespace engine
{

GameBase::GameBase()
#if defined(BT_SERVER)
: mGameSaveLoad(*this)
#endif // BT_SERVER
{
	game::FrameInterpolate::Register();
}

GameBase::~GameBase()
{
}

void GameBase::ProcessInput([[maybe_unused]] bool bLostFocus, game::MenuInput& rMenuInput)
{
#if defined(BT_CLIENT)
	game::gpInput->UpdateMenuInput(bLostFocus, rMenuInput);
#endif
	ProcessMenuInput(rMenuInput);
}

#if defined(BT_CLIENT)
void GameBase::ClientUpdate()
{
	game::gpClientSession->Poll();
	game::gpClientSession->Reconcile();

	if (game::gpClientSession->IsStalled())
	{
		return;
	}

	int64_t iFullTicks = mTimeStep.TickRealtime();
	if (mGameFlags & GameFlags::kPaused) [[unlikely]]
	{
		mTimeStep.ClearAccumulator();
		iFullTicks = 0;
	}
	PrepareActiveSet();

	const std::vector<GridCoord>& rActiveCoords = game::gpGame->mActiveCoords;

	gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
	for (int64_t i = 0; i < iFullTicks; ++i)
	{
		++miTickCounter;
		mfCurrentTime += game::kfDeltaTime;

		bool bExtrapolating = game::gpClientSession->IsExtrapolating();
		if (bExtrapolating)
		{
			game::gpClientSession->PrepareExtrapolationTick(rActiveCoords);
		}

		BuildAndDispatchFrameTicks(rActiveCoords, bExtrapolating);
		FinalizeFrameTick(rActiveCoords, bExtrapolating);
	}
	gpProfileManager->CpuStop(game::kCpuTimerFrameUpdate, false);

	if constexpr (kbEnableProfiling)
	{
		gpProfileManager->mFullUpdatesInTheLastSecond.Set(iFullTicks);
	}
}
#endif // BT_CLIENT

#if defined(BT_SERVER)
void GameBase::ServerUpdate(const game::MenuInput& rMenuInput)
{
	game::gpServerSession->PreTickNetwork();

	if (mGameSaveLoad.Quickload(rMenuInput)) [[unlikely]]
	{
		game::gpGame->ComputeActiveSet();
		return;
	}

	mGameSaveLoad.SaveLoadReplay();

	game::gpServerSession->WaitForTick(mTimeStep);

	int64_t iFullTicks = mTimeStep.TickRealtime();
	if (mTimeStep.mbTimeScaleChanged) [[unlikely]]
	{
		mTimeStep.mbTimeScaleChanged = false;
		gpServer->BroadcastTimespeedUpdate(mTimeStep.miTimeMultiply, mTimeStep.miTimeDivide);
		Log(kLogNetwork, "Timespeed changed Multiply: {} Divide: {}", mTimeStep.miTimeMultiply, mTimeStep.miTimeDivide);
	}
	if (mGameFlags & GameFlags::kPaused) [[unlikely]]
	{
		mTimeStep.ClearAccumulator();
		iFullTicks = 0;
	}
	else if (iFullTicks != 1) [[unlikely]]
	{
		Log("iFullTicks: {} != 1", iFullTicks);
	}
	PrepareActiveSet();

	const std::vector<GridCoord>& rActiveCoords = game::gpGame->mActiveCoords;

	gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
	for (int64_t i = 0; i < iFullTicks; ++i)
	{
		++miTickCounter;
		mfCurrentTime += game::kfDeltaTime;

		game::gpServerSession->PrepareTick();

		if (mGameSaveLoad.IsRecording() || mGameSaveLoad.IsReplaying()) [[unlikely]]
		{
			mGameSaveLoad.SyncReplayTick();
		}

		BuildAndDispatchFrameTicks(rActiveCoords, false);
		FinalizeFrameTick(rActiveCoords, false);
	}
	if (iFullTicks > 0)
	{
		game::gpServerSession->SendResends(miTickCounter);
	}
	gpProfileManager->CpuStop(game::kCpuTimerFrameUpdate, false);

	if constexpr (kbEnableProfiling)
	{
		gpProfileManager->mFullUpdatesInTheLastSecond.Set(iFullTicks);
	}

	mGameSaveLoad.Quicksave(rMenuInput);
}
#endif // BT_SERVER

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
			game::gpClientSession->BuildExtrapolationFrameRef(rCoord, pNext, pCurrent);
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
#endif // BT_CLIENT
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
		game::gpClientSession->RecordExtrapolationSnapshot(rActiveCoords, miTickCounter);
	}
	else
#endif
	{
		SwapFrames();
	}

#if defined(BT_SERVER)
	game::gpServerSession->BroadcastTick(miTickCounter);
#endif

	for (auto& [rCoord, rFrameInput] : game::gpGame->mFrameInputs)
	{
		rFrameInput.statusChanges.clear();
	}
}

#if defined(BT_CLIENT)
game::Frame& GameBase::RenderFrame(GridCoord coord) const
{
	if (game::gpClientSession->IsExtrapolating())
	{
		game::Frame* pFrame = game::gpClientSession->GetSnapshotFrame(coord);
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

	// Interpolate elapsed time with the sub-step remainder for smooth rendering
	float fCurrentTime = mfCurrentTime + std::max(0.0f, common::NanosecondsToFloatSeconds<float>(mTimeStep.mTickRemainderNs));
	gpGraphics->RenderGlobal(fCurrentTime);

	// Camera coord fallback: use human coord if available, else first active coord
	GridCoord cameraCoord = game::gpGame->mHumanGridCoord;
	if (!mCoordFrames.contains(cameraCoord) && !rActiveCoords.empty())
	{
		cameraCoord = rActiveCoords.front();
	}

	// Per-frame render interpolates (skip when no active frames)
	if (!rActiveCoords.empty())
	{
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
				game::FrameInterpolate::AllocateAndCopy(gpGraphics->mRenderInterpolates.try_emplace(rCoord).first->second, rFrame.interpolate);
				game::FrameInterpolate::Update(gpGraphics->mRenderInterpolates.at(rCoord), rFrame, fDeltaTime);
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

		// Decay visual error offset for smooth reconciliation corrections
		{
			float fDisplayDeltaTime = 1.0f / static_cast<float>(gpGraphics->miMonitorRefreshRate);
			float fDecay = std::exp(-game::Game::kfVisualErrorDecayRate * fDisplayDeltaTime);
			game::gpGame->mVecVisualErrorOffset = XMVectorScale(game::gpGame->mVecVisualErrorOffset, fDecay);
			if (XMVectorGetX(XMVector3Length(game::gpGame->mVecVisualErrorOffset)) < game::Game::kfVisualErrorMinDistance)
			{
				game::gpGame->mVecVisualErrorOffset = {};
			}
		}
	}

	// Write UI buffers on main thread (safe - Update() already complete)
	// Capture command buffer index before async launch to avoid re-reading in async thread
	int64_t iCommandBuffer = gpSwapchainManager->miFramebufferIndex;
	gpTextManager->RenderMain(iCommandBuffer);

	gpGraphics->RenderMainPresentAcquire(iCommandBuffer, gpGraphics->mRenderInterpolates, rActiveCoords, cameraCoord);

}
#endif // BT_CLIENT

void GameBase::PrepareActiveSet()
{
#if defined(BT_SERVER)
	if (mGameSaveLoad.IsReplaying())
	{
		// During replay, all recorded coords are active
		// Heap: vector clear/push_back, unordered_map insertion + make_unique<Frame>
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		game::gpGame->mActiveCoords.clear();
		for (const auto& [rCoord, rpReader] : mGameSaveLoad.GetReplayReaders())
		{
			game::gpGame->mActiveCoords.push_back(rCoord);
			CoordFrames& rSub = mCoordFrames.try_emplace(rCoord).first->second;
			if (rSub.pNext == nullptr)
			{
				rSub.pNext = std::make_unique<game::Frame>();
			}
		}
		game::gpGame->BuildFrameInputs();
	}
	else
#endif // BT_SERVER
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
#if defined(BT_SERVER)
	if (!mGameSaveLoad.IsReplaying())
	{
		game::gpGame->EnsureNextFrames();
	}
	else
#endif // BT_SERVER
	if (mCoordFrames.contains(game::gpGame->mHumanGridCoord)
		&& mCoordFrames.at(game::gpGame->mHumanGridCoord).pNext == nullptr)
	{
		// Heap: make_unique<Frame> for replay target coordinate
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		mCoordFrames.at(game::gpGame->mHumanGridCoord).pNext = std::make_unique<game::Frame>();
	}
}

} // namespace engine
