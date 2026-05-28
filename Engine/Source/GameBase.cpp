#include "GameBase.h"

#include "Game.h"
#if defined(BT_CLIENT)
#include "Network/Client/ClientSession.h"
#endif
#if defined(BT_SERVER)
#include "Network/Server/ServerSession.h"
#endif
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

GameBase::~GameBase()
{
}

void GameBase::ProcessInput([[maybe_unused]] bool bLostFocus, game::MenuInput& rMenuInput)
{
#if defined(BT_CLIENT)
	game::gpInput->UpdateMenuInput(bLostFocus, rMenuInput);
	game::gpInput->UpdateCameraInput();
#endif
	ProcessMenuInput(rMenuInput);
}

#if defined(BT_CLIENT)
void GameBase::ClientUpdate()
{
	common::LogTickScope logTickScope(miTickCounter);

	game::gpClientSession->Poll();

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

	game::gpClientSession->UpdateDesiredCoords(game::SubscriptionChangeReason::kPollTick);
	game::gpClientSession->UpdateSubscriptions();
	PrepareActiveSet();

	// Hard ceiling: sim must not pass latestServerTick - targetBehind, so StatusChanges arrive before
	// their tick is simulated. Extreme "sim way behind target" is handled by the snap path in Reconcile.
	int64_t iCeiling = game::gpClientSession->GetTargetSimTick();
	if (iCeiling >= 0 && iFullTicks > 0)
	{
		int64_t iRoomToAdvance = std::max<int64_t>(0, iCeiling - miTickCounter);
		if (iFullTicks > iRoomToAdvance)
		{
			mTimeStep.AbsorbUnusedTicks(iFullTicks - iRoomToAdvance);
			iFullTicks = iRoomToAdvance;
		}
	}

	// Set after the ceiling clamp so mfLastDeltaTime reflects the sim seconds actually applied this
	// iteration (kept in lockstep with miTickCounter / mfCurrentTime below). No client-side consumer
	// reads mfLastDeltaTime from inside PrepareActiveSet today; if one is added, move this earlier
	// and accept that it represents pre-clamp intent rather than executed work.
	mfLastDeltaTime = static_cast<float>(iFullTicks) * game::kfDeltaTime;

	miTickCounter += iFullTicks;
	mfCurrentTime += static_cast<float>(iFullTicks) * game::kfDeltaTime;

	gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
	game::gpClientSession->Reconcile();
	gpProfileManager->CpuStop(game::kCpuTimerFrameUpdate, false);

	if constexpr (kbProfiling)
	{
		gpProfileManager->mFullUpdatesInTheLastSecond.Set(iFullTicks);
	}
}
#endif // BT_CLIENT

#if defined(BT_SERVER)
void GameBase::ServerUpdate(const game::MenuInput& rMenuInput)
{
	common::LogTickScope logTickScope(miTickCounter);

	game::gpServerSession->PreTickNetwork();

	if (game::gpGame->mGameSaveLoad.Quickload(rMenuInput)) [[unlikely]]
	{
		game::gpGame->ComputeActiveSet();
		return;
	}

	game::gpGame->mGameSaveLoad.SaveLoadReplay();

	game::gpServerSession->WaitForTick(mTimeStep);

	int64_t iFullTicks = mTimeStep.TickRealtime();
	game::gpServerSession->BroadcastTimespeedIfChanged();
	if (mGameFlags & GameFlags::kPaused) [[unlikely]]
	{
		mTimeStep.ClearAccumulator();
		iFullTicks = 0;
	}
	else if (iFullTicks != 1) [[unlikely]]
	{
		LOG(kDefault, kWarning, "ServerUpdate FullTicks: {} (expected 1)", iFullTicks);
	}
	mfLastDeltaTime = static_cast<float>(iFullTicks) * game::kfDeltaTime;

	PrepareActiveSet();

	const std::vector<GridCoord>& rActiveCoords = game::gpGame->mActiveCoords;

	gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
	for (int64_t i = 0; i < iFullTicks; ++i)
	{
		++miTickCounter;
		mfCurrentTime += game::kfDeltaTime;

		common::LogTickScope perTickScope(miTickCounter);

		game::gpServerSession->PrepareTick();

		if (game::gpGame->mGameSaveLoad.IsRecording() || game::gpGame->mGameSaveLoad.IsReplaying()) [[unlikely]]
		{
			game::gpGame->mGameSaveLoad.SyncReplayTick();
		}

		BuildAndDispatchFrameTicks(rActiveCoords);
		FinalizeFrameTick(rActiveCoords);
	}
	if (iFullTicks > 0)
	{
		game::gpServerSession->SendResends(miTickCounter);
	}
	gpProfileManager->CpuStop(game::kCpuTimerFrameUpdate, true);

	if constexpr (kbProfiling)
	{
		gpProfileManager->mFullUpdatesInTheLastSecond.Set(iFullTicks);
	}

	game::gpGame->mGameSaveLoad.TickAutosave();
	game::gpGame->mGameSaveLoad.Quicksave(rMenuInput);
}
#endif // BT_SERVER

#if defined(BT_SERVER)
void GameBase::BuildAndDispatchFrameTicks(const std::vector<GridCoord>& rActiveCoords)
{
	const int64_t iActiveCount = static_cast<int64_t>(rActiveCoords.size());

	// Pre-resolve frame references to avoid repeated map lookups across all phases
	common::ScopedWorkbufferArena scopedWorkbufferArena = common::gpThreadLocal->mWorkbuffer.Push();
	for (int64_t j = 0; j < iActiveCount; ++j)
	{
		const GridCoord& rCoord = rActiveCoords[static_cast<size_t>(j)];
		auto& rFrames = mCoordFrames.at(rCoord);
		if (rFrames.pCurrent == nullptr || rFrames.pNext == nullptr)
		{
			LOG(kDefault, kWarning, "BuildDispatch NullFrame Coord: ({},{}) pCurrent: {} pNext: {}",
				rCoord.x, rCoord.y, rFrames.pCurrent != nullptr, rFrames.pNext != nullptr);
			continue;
		}
		common::gpThreadLocal->mWorkbuffer.PushBack<game::ActiveFrameRef>({
			.pNext = &NextFrame(rCoord),
			.pCurrent = &CurrentFrame(rCoord),
			.pFrameInput = &game::gpGame->mFrameInputs.at(rCoord),
			.pStaticData = &rFrames.staticData,
		});
	}
	std::span<const game::ActiveFrameRef> activeFrameRefs = common::gpThreadLocal->mWorkbuffer.Span<game::ActiveFrameRef>();

	gpProfileManager->CpuStart(game::kCpuTimerFrameInterpolate);
	gpProfileManager->CpuStart(game::kCpuTimerFramePostRender);

	const int64_t iFrameRefCount = static_cast<int64_t>(activeFrameRefs.size());
	auto processRange = [&](int64_t iBegin, int64_t iEnd)
	{
		for (int64_t j = iBegin; j < iEnd; ++j)
		{
			game::RunFrameTick(activeFrameRefs[j], miTickCounter, mfCurrentTime);
		}
	};
	if constexpr (kbFrameDispatch)
	{
		common::gpMultithreading->Dispatch(iFrameRefCount, processRange);
	}
	else
	{
		processRange(0, iFrameRefCount);
	}

	gpProfileManager->CpuStop(game::kCpuTimerFramePostRender, false);
	gpProfileManager->CpuStop(game::kCpuTimerFrameInterpolate, false);
}

void GameBase::FinalizeFrameTick(const std::vector<GridCoord>& rActiveCoords)
{
	// Transfer entities that crossed frame boundaries into destination frames
	if (!game::gpGame->mGameSaveLoad.IsReplaying())
	{
		game::gpGame->HarvestTransfers();
	}

	SwapFrames();

	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto it = mCoordFrames.find(rCoord);
		if (it == mCoordFrames.end() || it->second.pCurrent == nullptr || it->second.pNext == nullptr)
		{
			LOG(kDefault, kWarning, "PostSwap NullFrame Coord: ({},{}) Exists: {} pCurrent: {} pNext: {}",
				rCoord.x, rCoord.y, it != mCoordFrames.end(),
				it != mCoordFrames.end() && it->second.pCurrent != nullptr,
				it != mCoordFrames.end() && it->second.pNext != nullptr);
		}
	}

	game::gpServerSession->BroadcastTick(miTickCounter);

	for (auto& [rCoord, rFrameInput] : game::gpGame->mFrameInputs)
	{
		rFrameInput.statusChanges.clear();
	}
}
#endif // BT_SERVER

#if defined(BT_CLIENT)
game::Frame& GameBase::RenderFrame(GridCoord coord) const
{
	// Returns the frame kiRenderBehindTicks slots behind tail when possible so the render window
	// spans (source -> tail) — a true interpolation between two committed ticks, never
	// extrapolating past tail with its velocity. When the ring hasn't populated that many slots
	// yet (cold start or a replay/rollback that didn't apply retention), fall back to the oldest
	// available; callers force fDeltaTime = 0 for that coord so no extrapolation occurs.
	const CoordFrames& rFrames = mCoordFrames.at(coord);
	ASSERT(rFrames.iSnapshotCount > 0);
	int64_t iDesiredLogical = rFrames.iSnapshotCount - 1 - kiRenderBehindTicks;
	int64_t iLogical = std::max<int64_t>(0, iDesiredLogical);
	int64_t iPhysical = SnapshotIndex(rFrames.iSnapshotHead, iLogical);
	ASSERT(rFrames.snapshots[iPhysical] != nullptr);
	return *rFrames.snapshots[iPhysical];
}

void GameBase::ResetRenderClock()
{
	mfRenderTime = 0.0;
	mbRenderClockSeeded = false;
	mRenderTimer.Reset();
}

void GameBase::Render()
{
	const std::vector<GridCoord>& rActiveCoords = game::gpGame->mActiveCoords;

	// Interpolate elapsed time with the sub-step remainder for smooth rendering
	float fCurrentTime = mfCurrentTime + std::max(0.0f, common::NanosecondsToFloatSeconds<float>(mTimeStep.mTickRemainderNs));

	// Camera coord fallback: use client coord if available, else first active coord
	GridCoord cameraCoord = game::gpGame->mClientGridCoord;
	{
		auto it = mCoordFrames.find(cameraCoord);
		if ((it == mCoordFrames.end() || it->second.iSnapshotCount == 0) && !rActiveCoords.empty())
		{
			cameraCoord = rActiveCoords.front();
		}
	}

	// Per-frame render interpolates and camera update before RenderGlobal so that
	// f4RenderVisibleArea and lighting area are computed from the current camera position
	if (!rActiveCoords.empty())
	{
		gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
		gpProfileManager->CpuStart(game::kCpuTimerFrameInterpolate);

		// Render-side sim clock: advance mfRenderTime by sim delta (wall × current time ratio),
		// clamped to [T, T + kfDt]. Render integrates in the same units as T (sim seconds, not
		// wall seconds), so phase is preserved at any time ratio without an explicit servo.
		// Phase is set once via a one-shot seed at the window midpoint; after that, integration
		// preserves it automatically. On a single-tick commit, T advances +kfDt while mfRenderTime
		// stays continuous, so fDt drops by kfDt and the Update(N, kfDt) ≡ Update(N+1, 0)
		// invariant makes the handoff pixel-identical.
		double dSimDeltaSeconds = common::NanosecondsToFloatSeconds<double>(mTimeStep.WallToSim(mRenderTimer.GetDeltaNs(true)));
		mfLastRenderFrameSeconds = dSimDeltaSeconds;
		const CoordFrames& rCameraFrames = mCoordFrames.at(cameraCoord);
		bool bHaveInterpolationWindow = (rCameraFrames.iSnapshotCount >= kiRenderBehindTicks + 1);
		// RenderFrame already returns prev-tail when count>=2, else tail. Either way, its fCurrentTime
		// is the START of the current render window. Promoted to double so mfRenderTime - dT keeps
		// nanosecond precision even after hours of accumulated game time (float ULP at ~16384s is 2ms,
		// which would otherwise quantize the alpha and visibly stutter at high zoom).
		const game::Frame& rSourceFrame = RenderFrame(cameraCoord);
		double dT = rSourceFrame.interpolate.fCurrentTime;

		float fDeltaTime = 0.0f;
		bool bPaused = mGameFlags & GameFlags::kPaused;

		if (bPaused)
		{
			// Freeze. Don't advance mfRenderTime; rendered scene stays static until unpause.
			fDeltaTime = static_cast<float>(std::clamp(mfRenderTime - dT, 0.0, static_cast<double>(game::kfDeltaTime)));
		}
		else if (!bHaveInterpolationWindow)
		{
			// Cold start / single-snapshot coord: no prev-tail to interpolate from. Force fDt=0 so
			// rendering stays pinned to the only available frame — never extrapolating past tail
			// velocity. Reset the seed flag so the next steady-state entry re-seeds at midpoint.
			mfRenderTime = dT;
			mbRenderClockSeeded = false;
			fDeltaTime = 0.0f;
		}
		else
		{
			// One-shot seed at window midpoint so jitter has symmetric headroom before hitting
			// either clamp. After seeding, wall-rate integration preserves phase.
			if (!mbRenderClockSeeded)
			{
				mbRenderClockSeeded = true;
				mfRenderTime = dT + 0.5 * game::kfDeltaTime;
			}

			// Rebase only on multi-tick T regression (reconcile snap, full-state seed). Tolerance
			// widens to [T - kfDt, T + 2*kfDt] so a single-tick commit — which leaves mfRenderTime
			// anywhere from slightly below new T to slightly below new T+kfDt — never triggers a
			// rebase. Rebasing on every commit was the 32 Hz vibration signature.
			if (mfRenderTime < dT - game::kfDeltaTime || mfRenderTime > dT + 2.0 * game::kfDeltaTime)
			{
				mfRenderTime = dT + 0.5 * game::kfDeltaTime;
			}

			mfRenderTime += dSimDeltaSeconds;
			mfRenderTime = std::clamp(mfRenderTime, dT, dT + static_cast<double>(game::kfDeltaTime));
			fDeltaTime = static_cast<float>(mfRenderTime - dT);
		}
		{
			ScopedSuppressAllocationTracking suppress;

			// Heap: std::erase_if may rehash, operator[] may insert — suppressed like the old MergeFramesForRender
			// Remove render interpolates for deactivated coords
			std::erase_if(gpGraphics->mRenderInterpolates, [&](const std::pair<const GridCoord, game::FrameInterpolate>& rPair)
			{
				return !std::ranges::contains(rActiveCoords, rPair.first);
			});

			// AllocateAndCopy + Update each active frame's render interpolate (camera frame first).
			// Per-coord fDt override: a coord with only one snapshot has no prev-tail to interpolate
			// from, so force fDt=0 for that coord regardless of the camera-anchored global fDeltaTime.
			// This prevents a transient single-snapshot coord (post-fast-path shrink) from rendering
			// extrapolated-from-tail state.
			auto interpolateFrame = [&](const GridCoord& rCoord)
			{
				const game::Frame& rFrame = RenderFrame(rCoord);
				CoordFrames& rSub = mCoordFrames.at(rCoord);
				if (rFrame.interpolate.iTick < rSub.iLastRenderedTick ||
					(rFrame.interpolate.iTick == rSub.iLastRenderedTick && rFrame.interpolate.fCurrentTime < rSub.fLastRenderedTime))
				{
					LOG(kNetwork, kError, "Render regressed to older frame Coord: ({},{}) Tick: {} LastTick: {} Time: {} LastTime: {}", rCoord.x, rCoord.y, rFrame.interpolate.iTick, rSub.iLastRenderedTick, rFrame.interpolate.fCurrentTime, rSub.fLastRenderedTime);
 					DEBUG_BREAK();
				}
				rSub.iLastRenderedTick = rFrame.interpolate.iTick;
				rSub.fLastRenderedTime = rFrame.interpolate.fCurrentTime;
				float fCoordDeltaTime = (rSub.iSnapshotCount >= kiRenderBehindTicks + 1) ? fDeltaTime : 0.0f;
				game::FrameInterpolate::AllocateAndCopy(gpGraphics->mRenderInterpolates.try_emplace(rCoord).first->second, rFrame.interpolate);
				game::FrameInterpolate::Update(gpGraphics->mRenderInterpolates.at(rCoord), rFrame, fCoordDeltaTime);
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

		if constexpr (kbProfiling)
		{
			gpProfileManager->mInterpolateUpdatesInTheLastSecond.Set();
		}

		// Update camera before RenderGlobal
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

	gpGraphics->RenderGlobal(fCurrentTime);

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
	if (game::gpGame->mGameSaveLoad.IsReplaying())
	{
		// During replay, all recorded coords are active
		// Heap: vector clear/push_back, unordered_map insertion + make_unique<Frame>
		ScopedSuppressAllocationTracking suppress;
		game::gpGame->mActiveCoords.clear();
		for (const auto& [rCoord, rpReader] : game::gpGame->mGameSaveLoad.GetReplayReaders())
		{
			game::gpGame->mActiveCoords.push_back(rCoord);
			CoordFrames& rSub = mCoordFrames.try_emplace(rCoord).first->second;
			if (rSub.pNext == nullptr)
			{
				rSub.pNext = std::make_unique<game::Frame>();
			}
		}
		game::gpGame->BuildFrameInputs();
		return;
	}
#endif // BT_SERVER

	game::gpGame->ComputeActiveSet();
#if defined(BT_SERVER)
	game::gpGame->EnsureNextFrames();
#endif
	game::gpGame->BuildFrameInputs();
}

#if defined(BT_SERVER)
void GameBase::SwapFrames()
{
	for (auto& [rCoord, rFrames] : mCoordFrames)
	{
		std::swap(rFrames.pCurrent, rFrames.pNext);
	}

	// After swap, .next holds old current frames (stale data, reusable memory).
	// Ensure active entries exist for next iteration's AllocateAndCopy.
	if (!game::gpGame->mGameSaveLoad.IsReplaying())
	{
		game::gpGame->EnsureNextFrames();
	}
	else if (mCoordFrames.contains(game::gpGame->mClientGridCoord)
		&& mCoordFrames.at(game::gpGame->mClientGridCoord).pNext == nullptr)
	{
		// Heap: make_unique<Frame> for replay target coordinate
		ScopedSuppressAllocationTracking suppress;
		mCoordFrames.at(game::gpGame->mClientGridCoord).pNext = std::make_unique<game::Frame>();
	}
}
#endif // BT_SERVER

} // namespace engine
