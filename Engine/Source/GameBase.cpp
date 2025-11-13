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

	gCamera.mRealTime.Reset();

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

		// DT: TEMP Remove
		ProcessSavesAndReplays(rMenuInput, frameInputHeld, frameInputPressed);

		WriteFrameInterpolateBase(NextFrame(), CurrentFrame(), frameInputHeld, game::kfDeltaTime);
		WriteFramePostRenderBase(NextFrame(), CurrentFrame(), frameInputHeld, frameInputPressed);
		std::swap(mpCurrentFrame, mpNextFrame);
	#if defined(ENABLE_PROFILING)
		gpProfileManager->mFullUpdatesInTheLastSecond.Set();
	#endif
	}

	if (fDeltaTime <= kfEpsilon)
	{
		gpGraphics->RenderPresentAcquire(CurrentFrame());
		return;
	}

	LoadFromReplayHeld(frameInputHeld);

	// Create interpolated frame for smooth rendering between physics steps
	WriteFrameInterpolateBase(NextFrame(), CurrentFrame(), frameInputHeld, fDeltaTime);
#if defined(ENABLE_PROFILING)
	gpProfileManager->mInterpolateUpdatesInTheLastSecond.Set();
#endif

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

		LOG("Start recording replay at {}", CurrentFrame().interpolate.iFrame);
		mpDifferenceStreamWriterHeld = std::make_unique<engine::DifferenceStreamWriter<game::Frame, game::FrameInputHeld>>(CurrentFrame(), rFrameInputHeld);
		mpDifferenceStreamWriterPressed = std::make_unique<engine::DifferenceStreamWriter<game::Frame, game::FrameInputPressed>>(CurrentFrame(), rFrameInputPressed);

		return;
	}
	else if (mbSaveReplay && mpDifferenceStreamWriterHeld != nullptr)
	{
		mbSaveReplay = false;

		LOG("Saving replay at {}", CurrentFrame().interpolate.iFrame);
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
			// DT: TEMP Not needed? memcpy(&NextFrame(), &CurrentFrame(), sizeof(NextFrame()));

			if (mpDifferenceStreamReaderHeld->Loaded() && mpDifferenceStreamReaderPressed->Loaded())
			{
				LOG("Loaded replay at {}", CurrentFrame().interpolate.iFrame);
			}
			else
			{
				mpDifferenceStreamReaderHeld.reset();
				mpDifferenceStreamReaderPressed.reset();
			}

			return;
		}
	}

	UpdateDifferenceStream(CurrentFrame().interpolate.iFrame, rFrameInputHeld, true, mpDifferenceStreamWriterHeld, mpDifferenceStreamReaderHeld, "held");
	UpdateDifferenceStream(CurrentFrame().interpolate.iFrame, rFrameInputPressed, true, mpDifferenceStreamWriterPressed, mpDifferenceStreamReaderPressed, "pressed");
#endif
}

void GameBase::LoadFromReplayHeld([[maybe_unused]] game::FrameInputHeld& rFrameInputHeld)
{
#if defined(ENABLE_DEBUG_INPUT)
	if (mpDifferenceStreamReaderHeld != nullptr) [[unlikely]]
	{
		mpDifferenceStreamReaderHeld->Update(CurrentFrame().interpolate.iFrame, rFrameInputHeld, false);
	}
#endif
}

} // namespace engine
