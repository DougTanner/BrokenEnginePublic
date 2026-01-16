#include "GameBase.h"

#include "Audio/AudioManager.h"
#include "Graphics/Graphics.h"

#include "Frame/Frame.h"
#include "Frame/Render.h"
#include "Input/Input.h"

namespace engine
{

using enum MenuFlags;

GameBase::GameBase()
{
	game::FrameInterpolate::Register();
}

void GameBase::ResetRealTime()
{
	gpAudioManager->mRealTime.Reset();

	game::gpCamera->mRealTime.Reset();

	mTimeStep.Reset();
}

void GameBase::UpdateFramesAndRender(const game::MenuInput& rMenuInput, bool bLostFocus, bool bUpdateFrames)
{
	if (Quickload(rMenuInput)) [[unlikely]]
	{
		return;
	}

	SaveLoadReplay(rMenuInput);

	// Camera-dependent global rendering
	gpGraphics->RenderGlobal(CurrentFrame());

	// Perform full updates at fixed timestep
	CPU_PROFILE_START(kCpuTimerFrameUpdate);

	int64_t iFullUpdates = mTimeStep.UpdateRealtime(bLostFocus);
	if (!bUpdateFrames)
	{
		iFullUpdates = 0;
	}
	game::FrameInput frameInput = game::RawInputToFrameInput(gpRawInputManager->mRawInput);
	game::gpInput->UpdateFrameInputPressed(gpRawInputManager->mRawInput, frameInput);
	for (int64_t i = 0; i < iFullUpdates; ++i)
	{
		SyncReplay(CurrentFrame(), frameInput);

		CPU_PROFILE_START(kCpuTimerFrameInterpolate);
		game::FrameInterpolate::AllocateAndCopy(NextFrame().interpolate, CurrentFrame().interpolate);
		game::FrameInterpolate::Update(NextFrame().interpolate, CurrentFrame(), game::kfDeltaTime);
		CPU_PROFILE_STOP(kCpuTimerFrameInterpolate);

		CPU_PROFILE_START(kCpuTimerFramePostRender);
		game::FramePostRender::AllocateAndCopy(NextFrame().postRender, CurrentFrame().postRender);
		game::FramePostRender::Update(NextFrame(), CurrentFrame(), game::kfDeltaTime, frameInput);
		game::FramePostRender::PreCollision(NextFrame(), CurrentFrame(), game::kfDeltaTime);
		Collision::Collide();
		game::FramePostRender::PostCollision(NextFrame(), CurrentFrame(), game::kfDeltaTime);
		game::FramePostRender::AreaDamage(NextFrame(), CurrentFrame(), game::kfDeltaTime);
		game::FramePostRender::Destroy(NextFrame(), game::kfDeltaTime);
		game::FramePostRender::Spawn(NextFrame(), game::kfDeltaTime);
		CPU_PROFILE_STOP(kCpuTimerFramePostRender);

		std::swap(mpCurrentFrame, mpNextFrame);

		frameInput.ClearPressed();
	}
#if defined(ENABLE_PROFILING)
	gpProfileManager->mFullUpdatesInTheLastSecond.Set(iFullUpdates);
#endif

	// Create interpolated frame for smooth rendering
	float fDeltaTime = bUpdateFrames ? common::NanosecondsToFloatSeconds<float>(mTimeStep.mUpdateRemainderNs) : 0.0f;
	CPU_PROFILE_START(kCpuTimerFrameInterpolate);
	game::FrameInterpolate::AllocateAndCopy(NextFrame().interpolate, CurrentFrame().interpolate);
	game::FrameInterpolate::Update(NextFrame().interpolate, CurrentFrame(), fDeltaTime);
	CPU_PROFILE_STOP(kCpuTimerFrameInterpolate);
#if defined(ENABLE_PROFILING)
	gpProfileManager->mInterpolateUpdatesInTheLastSecond.Set();
#endif

	CPU_PROFILE_STOP(kCpuTimerFrameUpdate);

	// Render and present the interpolated frame
	gpGraphics->RenderMainPresentAcquire(NextFrame());

	// Quicksave
	Quicksave(rMenuInput);
}

void GameBase::Quicksave([[maybe_unused]] const game::MenuInput& rMenuInput)
{
#if defined(ENABLE_DEBUG_INPUT)
	if (rMenuInput.flags & game::MenuInputFlags::kQuicksave)
	{
		WriteVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kWrite}, QuicksaveFile(), CurrentFrame());
	}
#endif
}

bool GameBase::Quickload([[maybe_unused]] const game::MenuInput& rMenuInput)
{
#if defined(ENABLE_DEBUG_INPUT)
	if (rMenuInput.flags & game::MenuInputFlags::kQuickload || rMenuInput.flags & game::MenuInputFlags::kResetFrame)
	{
		if (rMenuInput.flags & game::MenuInputFlags::kQuickload)
		{
			ReadVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, QuicksaveFile(), CurrentFrame());
		}
		else
		{
			mpCurrentFrame = std::make_unique<game::Frame>();
			mpCurrentFrame->interpolate.flags |= game::FrameFlags::kGame;
		}

		Reset();

		return true;
	}
#endif

	return false;
}

void GameBase::SaveLoadReplay([[maybe_unused]] const game::MenuInput& rMenuInput)
{
#if defined(ENABLE_DEBUG_INPUT)
	if (rMenuInput.flags & game::MenuInputFlags::kSaveReplay)
	{
		mbSaveReplay = true;
	}
	else if (rMenuInput.flags & game::MenuInputFlags::kLoadReplay)
	{
		mbLoadReplay = true;
	}
#endif
}

void GameBase::SyncReplay([[maybe_unused]] game::Frame& rFrame, [[maybe_unused]] game::FrameInput& rFrameInput)
{
#if defined(ENABLE_DEBUG_INPUT)
	if (mbSaveReplay && mpDifferenceStreamWriter == nullptr)
	{
		mbSaveReplay = false;
		mpDifferenceStreamReader.reset();
		mpDifferenceStreamWriter = std::make_unique<DifferenceStreamWriter<game::Frame, game::FrameInput>>(rFrame, rFrameInput);
		return;
	}
	else if (mbSaveReplay && mpDifferenceStreamWriter != nullptr)
	{
		mbSaveReplay = false;
		mpDifferenceStreamWriter->Save({FileFlags::kAppDataDirectory, FileFlags::kWrite, FileFlags::kBackup}, std::filesystem::path("F7.replay"), rFrame);
		mpDifferenceStreamWriter.reset();
		return;
	}

	if (mbLoadReplay)
	{
		mbLoadReplay = false;

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
			LOG("End replay {}, looping", rFrame.interpolate.iFrame);
			common::BreakOnNotEqual(rFrame, mpDifferenceStreamReader->GetSavedEnd());
			mpDifferenceStreamReader.reset();
			mbLoadReplay = true;
		}
	}
#endif
}

} // namespace engine
