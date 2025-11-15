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

	game::FrameInputHeld frameInputHeld = game::RawInputToFrameInputHeld(gpRawInputManager->mRawInput);

	// Calculate how many fixed timestep updates are needed based on accumulated time
	int64_t iFullUpdates = mTimeStep.UpdateRealtime(bLostFocus);
	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(mTimeStep.mUpdateRemainderNs);
	if (fDeltaTime > kfEpsilon)
	{
		gpGraphics->RenderGlobal(CurrentFrame());
	}

	// Perform full physics updates at fixed timestep
	for (int64_t i = 0; i < iFullUpdates; ++i)
	{
		game::FrameInputPressed frameInputPressed = game::gpInput->UpdateFrameInputPressed(gpRawInputManager->mRawInput);
		SyncReplay(frameInputHeld, frameInputPressed);

		game::Frame::UpdateInterpolate(NextFrame(), CurrentFrame(), game::kfDeltaTime);
		game::Frame::UpdatePostRender(NextFrame(), CurrentFrame(), frameInputHeld, frameInputPressed, game::kfDeltaTime);
		std::swap(mpCurrentFrame, mpNextFrame);
	}
#if defined(ENABLE_PROFILING)
	gpProfileManager->mFullUpdatesInTheLastSecond.Set(iFullUpdates);
#endif

	if (fDeltaTime <= kfEpsilon)
	{
		gpGraphics->RenderPresentAcquire(CurrentFrame());
		return;
	}

	// DT: TODO No longer needed?
	LoadFromReplayHeld(frameInputHeld);

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
			new (mpCurrentFrame.get()) game::Frame(game::FrameFlags::kGame);
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

void GameBase::SyncReplay([[maybe_unused]] game::FrameInputHeld& rFrameInputHeld, [[maybe_unused]] game::FrameInputPressed& rFrameInputPressed)
{
#if defined(ENABLE_DEBUG_INPUT)
	if (mbSaveReplay && mpDifferenceStreamWriterHeld == nullptr)
	{
		mbSaveReplay = false;

		mpDifferenceStreamReaderHeld.reset();
		mpDifferenceStreamReaderPressed.reset();

		LOG("Start recording replay at {}", CurrentFrame().iFrame);
		mpDifferenceStreamWriterHeld = std::make_unique<engine::DifferenceStreamWriter<game::Frame, game::FrameInputHeld>>(CurrentFrame(), rFrameInputHeld);
		mpDifferenceStreamWriterPressed = std::make_unique<engine::DifferenceStreamWriter<game::Frame, game::FrameInputPressed>>(CurrentFrame(), rFrameInputPressed);

		return;
	}
	else if (mbSaveReplay && mpDifferenceStreamWriterHeld != nullptr)
	{
		mbSaveReplay = false;

		LOG("Saving replay at {}", CurrentFrame().iFrame);
		mpDifferenceStreamWriterHeld->Save({FileFlags::kAppDataDirectory, FileFlags::kWrite, FileFlags::kBackup}, std::filesystem::path("F7Held.replay"), CurrentFrame());
		mpDifferenceStreamWriterPressed->Save({FileFlags::kAppDataDirectory, FileFlags::kWrite, FileFlags::kBackup}, std::filesystem::path("F7Pressed.replay"), CurrentFrame());
		mpDifferenceStreamWriterHeld.reset();
		mpDifferenceStreamWriterPressed.reset();

		return;
	}

	if (mbLoadReplay)
	{
		mbLoadReplay = false;

		if (mpDifferenceStreamReaderHeld != nullptr)
		{
			mpDifferenceStreamReaderHeld.reset();
			mpDifferenceStreamReaderPressed.reset();

			return;
		}
		else
		{
			Reset();
			mpDifferenceStreamReaderHeld = std::make_unique<engine::DifferenceStreamReader<game::Frame, game::FrameInputHeld>>(engine::FileFlags_t {FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7Held.replay"), CurrentFrame(), rFrameInputHeld);
			mpDifferenceStreamReaderPressed = std::make_unique<engine::DifferenceStreamReader<game::Frame, game::FrameInputPressed>>(engine::FileFlags_t {FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7Pressed.replay"), CurrentFrame(), rFrameInputPressed);

			if (mpDifferenceStreamReaderHeld->Loaded() && mpDifferenceStreamReaderPressed->Loaded())
			{
				LOG("Loaded replay at {}", CurrentFrame().iFrame);
			}
			else
			{
				mpDifferenceStreamReaderHeld.reset();
				mpDifferenceStreamReaderPressed.reset();
			}

			return;
		}
	}

	UpdateDifferenceStream(CurrentFrame().iFrame, rFrameInputHeld, true, mpDifferenceStreamWriterHeld, mpDifferenceStreamReaderHeld, "held");
	UpdateDifferenceStream(CurrentFrame().iFrame, rFrameInputPressed, true, mpDifferenceStreamWriterPressed, mpDifferenceStreamReaderPressed, "pressed");
#endif
}

void GameBase::LoadFromReplayHeld([[maybe_unused]] game::FrameInputHeld& rFrameInputHeld)
{
#if defined(ENABLE_DEBUG_INPUT)
	if (mpDifferenceStreamReaderHeld != nullptr) [[unlikely]]
	{
		mpDifferenceStreamReaderHeld->Update(CurrentFrame().iFrame, rFrameInputHeld, false);
	}
#endif
}

} // namespace engine
