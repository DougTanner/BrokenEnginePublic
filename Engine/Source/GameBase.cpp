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
	game::Frame::RegisterTypes();
}

void GameBase::ResetRealTime()
{
	gpAudioManager->mRealTime.Reset();

	game::gpCamera->mRealTime.Reset();

	mTimeStep.Reset();
}

void GameBase::UpdateFramesAndRender(const game::MenuInput& rMenuInput, bool bLostFocus)
{
	if (Quickload(rMenuInput)) [[unlikely]]
	{
		return;
	}

	SaveLoadReplay(rMenuInput);

	// Calculate how many fixed timestep updates are needed based on accumulated time
	int64_t iFullUpdates = mTimeStep.UpdateRealtime(bLostFocus);
	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(mTimeStep.mUpdateRemainderNs);
	if (fDeltaTime > kfEpsilon)
	{
		gpGraphics->RenderGlobal(CurrentFrame());
	}

	// Perform full updates at fixed timestep
	game::FrameInput frameInput = game::RawInputToFrameInput(gpRawInputManager->mRawInput);
	game::gpInput->UpdateFrameInputPressed(gpRawInputManager->mRawInput, frameInput);
	for (int64_t i = 0; i < iFullUpdates; ++i)
	{
		SyncReplay(CurrentFrame(), frameInput);

		game::Frame::UpdateInterpolate(NextFrame(), CurrentFrame(), game::kfDeltaTime);
		game::Frame::UpdatePostRender(NextFrame(), CurrentFrame(), frameInput, game::kfDeltaTime);
		std::swap(mpCurrentFrame, mpNextFrame);

		frameInput.ClearPressed();
	}
#if defined(ENABLE_PROFILING)
	gpProfileManager->mFullUpdatesInTheLastSecond.Set(iFullUpdates);
#endif

	if (fDeltaTime <= kfEpsilon)
	{
		gpGraphics->RenderPresentAcquire(CurrentFrame());
		return;
	}

	// Create interpolated frame for smooth rendering
	game::Frame::UpdateInterpolate(NextFrame(), CurrentFrame(), fDeltaTime);
#if defined(ENABLE_PROFILING)
	gpProfileManager->mInterpolateUpdatesInTheLastSecond.Set();
#endif

	// Render and present the interpolated frame
	gpGraphics->RenderMainImagePresentAcquire(NextFrame());

	Quicksave(rMenuInput);
}

void GameBase::Quicksave([[maybe_unused]] const game::MenuInput& rMenuInput)
{
#if defined(ENABLE_DEBUG_INPUT)
	if (rMenuInput.flags & game::MenuInputFlags::kQuicksave)
	{
		engine::WriteVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kWrite}, QuicksaveFile(), CurrentFrame());
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
			engine::ReadVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, QuicksaveFile(), CurrentFrame());
		}
		else
		{
			mpCurrentFrame = std::make_unique<game::Frame>(game::FrameFlags::kGame);
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
		mpDifferenceStreamWriter = std::make_unique<engine::DifferenceStreamWriter<game::Frame, game::FrameInput>>(rFrame, rFrameInput);
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
			mpDifferenceStreamReader = std::make_unique<engine::DifferenceStreamReader<game::Frame, game::FrameInput>>(engine::FileFlags_t {FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7.replay"), rFrame, rFrameInput);

			if (!mpDifferenceStreamReader->Loaded())
			{
				mpDifferenceStreamReader.reset();
			}

			return;
		}
	}

	if (mpDifferenceStreamWriter != nullptr) [[unlikely]]
	{
		mpDifferenceStreamWriter->Update(rFrame.iFrame, rFrameInput, rFrame);
	}
	else if (mpDifferenceStreamReader != nullptr) [[unlikely]]
	{
		if (!mpDifferenceStreamReader->Update(rFrame.iFrame, rFrameInput, rFrame))
		{
			LOG("End replay {}", rFrame.iFrame);
			common::BreakOnNotEqual(rFrame, mpDifferenceStreamReader->GetSavedEnd());
			mpDifferenceStreamReader.reset();
		}
	}
#endif
}

} // namespace engine
