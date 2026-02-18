#include "GameBase.h"

#include "Audio/AudioManager.h"
#include "Graphics/Graphics.h"
#include "Graphics/Managers/SwapchainManager.h"
#include "Graphics/Managers/TextManager.h"
#include "Input/RawInputManager.h"
#include "Profile/ProfileManager.h"

#include "Game.h"
#include "Frame/Frame.h"
#include "Frame/HealthDamage.h"
#include "Frame/Render.h"
#include "Input/Input.h"

namespace engine
{

using enum MenuFlags;

void ResetRealTime()
{
	gpAudioManager->mRealTime.Reset();
	game::gpCamera->mRealTime.Reset();
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
		gpRawInputManager->SetVibration(0, 0.0f, 0.0f);
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

void GameBase::UpdateFramesAndRender(const game::MenuInput& rMenuInput, bool bLostFocus, bool bUpdateFrames)
{
	if (Quickload(rMenuInput)) [[unlikely]]
	{
		return;
	}

	SaveLoadReplay(rMenuInput);

	// Perform full updates at fixed timestep
	int64_t iFullUpdates = mTimeStep.UpdateRealtime(bLostFocus);
	if (!bUpdateFrames)
	{
		iFullUpdates = 0;
	}
	game::FrameInput frameInput = game::RawInputToFrameInput(gpRawInputManager->mRawInput);
	game::gpInput->UpdateFrameInputPressed(gpRawInputManager->mRawInput, frameInput);
	game::gpGame->UpdateAiInput(CurrentFrame(), frameInput);
	gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
	for (int64_t i = 0; i < iFullUpdates; ++i)
	{
		SyncReplay(CurrentFrame(), frameInput);

		gpProfileManager->CpuStart(game::kCpuTimerFrameInterpolate);
		game::FrameInterpolate::AllocateAndCopy(NextFrame().interpolate, CurrentFrame().interpolate);
		game::FrameInterpolate::Update(NextFrame().interpolate, CurrentFrame(), game::kfDeltaTime);
		gpProfileManager->CpuStop(game::kCpuTimerFrameInterpolate, false);

		gpProfileManager->CpuStart(game::kCpuTimerFramePostRender);
		game::FramePostRender::AllocateAndCopy(NextFrame().postRender, CurrentFrame().postRender);
		game::FramePostRender::Update(NextFrame(), CurrentFrame(), frameInput);
		game::FramePostRender::PreCollision(NextFrame(), CurrentFrame());
		Collision::Collide(NextFrame().postRender.alignments, NextFrame().postRender.vecArea);
		game::FramePostRender::PostCollision(NextFrame(), CurrentFrame());
		game::FramePostRender::AreaDamage(NextFrame(), CurrentFrame());
		game::FramePostRender::Destroy(NextFrame());
		game::FramePostRender::Spawn(NextFrame());
		gpProfileManager->CpuStop(game::kCpuTimerFramePostRender, false);

		std::swap(mpCurrentFrame, mpNextFrame);

		frameInput.ClearPressed();
	}
	gpProfileManager->CpuStop(game::kCpuTimerFrameUpdate, false);

	if constexpr (kbEnableProfiling)
	{
		gpProfileManager->mFullUpdatesInTheLastSecond.Set(iFullUpdates);
	}

	// Wait for previous render to complete before submitting new commands
	gpGraphics->WaitForRender();

	// Interpolate elapsed time with the sub-step remainder for smooth rendering
	float fCurrentTime = CurrentFrame().interpolate.fCurrentTime + (bUpdateFrames ? common::NanosecondsToFloatSeconds<float>(mTimeStep.mUpdateRemainderNs) : 0.0f);
	gpGraphics->RenderGlobal(CurrentFrame(), fCurrentTime);

	// Write to temporary interpolated-only frame
	gpProfileManager->CpuStart(game::kCpuTimerFrameUpdate);
	gpProfileManager->CpuStart(game::kCpuTimerFrameInterpolate);
	float fDeltaTime = bUpdateFrames ? common::NanosecondsToFloatSeconds<float>(mTimeStep.mUpdateRemainderNs) : 0.0f;
	game::FrameInterpolate::AllocateAndCopy(*gpGraphics->mpFrameInterpolate, CurrentFrame().interpolate);
	game::FrameInterpolate::Update(*gpGraphics->mpFrameInterpolate, CurrentFrame(), fDeltaTime);
	gpProfileManager->CpuStop(game::kCpuTimerFrameInterpolate, false);
	gpProfileManager->CpuStop(game::kCpuTimerFrameUpdate, false);
	if constexpr (kbEnableProfiling)
	{
		gpProfileManager->mInterpolateUpdatesInTheLastSecond.Set();
	}

	// Update camera before async launch
	game::gpCamera->Update(*gpGraphics->mpFrameInterpolate);

	// Write UI buffers on main thread (safe - Update() already complete)
	// Capture command buffer index before async launch to avoid re-reading in async thread
	int64_t iCommandBuffer = gpSwapchainManager->miFramebufferIndex;
	gpTextManager->RenderMain(iCommandBuffer);

	// Launch async render with captured index
	if constexpr (kbEnableRenderThread)
	{
		gpGraphics->mRenderFuture.Wake([iCommandBuffer]()
		{
			gpGraphics->RenderMainPresentAcquire(iCommandBuffer, *gpGraphics->mpFrameInterpolate);
		});
	}
	else
	{
		gpGraphics->RenderMainPresentAcquire(iCommandBuffer, *gpGraphics->mpFrameInterpolate);
	}

	// Quicksave
	Quicksave(rMenuInput);
}

void GameBase::Quicksave([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	ScopedSuppressAllocationTracking suppressTracking;

	if constexpr (kbEnableDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kQuicksave)
		{
			WriteVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kWrite}, QuicksaveFile(), CurrentFrame());
		}
	}
}

bool GameBase::Quickload([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	ScopedSuppressAllocationTracking suppressTracking;

	if constexpr (kbEnableDebugInput)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kQuickload || rMenuInput.flags & game::MenuInputFlags::kResetFrame)
		{
			if (rMenuInput.flags & game::MenuInputFlags::kQuickload)
			{
				ReadVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, QuicksaveFile(), CurrentFrame());
			}
			else
			{
				game::gpGame->CreateNewFrame(game::FrameFlags::kGame);
			}

			Reset();

			return true;
		}
	}

	return false;
}

void GameBase::SaveLoadReplay([[maybe_unused]] const game::MenuInput& rMenuInput)
{
	ScopedSuppressAllocationTracking suppressTracking;

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
	}
}

void GameBase::SyncReplay([[maybe_unused]] game::Frame& rFrame, [[maybe_unused]] game::FrameInput& rFrameInput)
{
	ScopedSuppressAllocationTracking suppressTracking;

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
			return;
		}

		if (mGameFlags & GameFlags::kLoadReplay)
		{
			mGameFlags.Clear(GameFlags::kLoadReplay);

			if (mpDifferenceStreamReader != nullptr)
			{
				mpDifferenceStreamReader.reset();

				return;
			}
			else
			{
				Reset();
				mpDifferenceStreamReader = std::make_unique<DifferenceStreamReader<game::Frame, game::FrameInput>>(FileFlags_t {FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7.replay"), rFrame, rFrameInput);

				if (!mpDifferenceStreamReader->Loaded())
				{
					mpDifferenceStreamReader.reset();
				}

				return;
			}
		}

		if (mpDifferenceStreamWriter != nullptr) [[unlikely]]
		{
			mpDifferenceStreamWriter->Update(rFrame.interpolate.iFrame, rFrameInput, rFrame);
		}
		else if (mpDifferenceStreamReader != nullptr) [[unlikely]]
		{
			if (!mpDifferenceStreamReader->Update(rFrame.interpolate.iFrame, rFrameInput, rFrame))
			{
				Log("End replay {}, looping", rFrame.interpolate.iFrame);
				common::BreakOnNotEqual(rFrame, mpDifferenceStreamReader->GetSavedEnd());
				mpDifferenceStreamReader.reset();
				mGameFlags.Set(GameFlags::kLoadReplay);
			}
		}
	}
}

} // namespace engine
