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
	mTimeStep.Reset();
	gpAudioManager->mRealTime.Reset();
}

void GameBase::UpdateFramesAndRender(const game::MenuInput& rMenuInput, bool bLostFocus)
{
	// Quickload
	if (Quickload(rMenuInput)) [[unlikely]]
	{
		return;
	}

	SaveLoadReplay(rMenuInput);

	game::FrameInputHeld frameInputHeld = game::RawInputToFrameInputHeld(gpRawInputManager->mRawInput);

	int64_t iFullUpdates = mTimeStep.UpdateRealtime(bLostFocus);
	for (int64_t i = 0; i < iFullUpdates; ++i)
	{
		game::FrameInputPressed frameInputPressed = game::gpInput->UpdateFrameInputPressed(gpRawInputManager->mRawInput);
		SyncReplay(frameInputHeld, frameInputPressed);

		// DT: TEMP Remove
		ProcessSavesAndReplays(rMenuInput, frameInputHeld, frameInputPressed);

		// Do a full frame update
		WriteFrameCameraBase(NextFrame(), CurrentFrame(), game::kfDeltaTime);
		WriteFrameInterpolateBase(NextFrame(), CurrentFrame(), frameInputHeld);
		WriteFramePostRenderBase(NextFrame(), CurrentFrame(), frameInputHeld, frameInputPressed);
		std::swap(mpCurrentFrame, mpNextFrame);
	}

	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(mTimeStep.mUpdateRemainderNs);
	if (fDeltaTime <= kfEpsilon)
	{
		gpGraphics->RenderPresentAcquire(CurrentFrame());
		return;
	}

	// If a replay is playing back, make sure to grab the correct held input
	LoadFromReplayHeld(frameInputHeld);

	// Update global frame and trigger global rendering
	WriteFrameCameraBase(NextFrame(), CurrentFrame(), fDeltaTime);
	gpGraphics->RenderGlobal(NextFrame());

	// Create an interpolated frame for final rendering
	WriteFrameInterpolateBase(NextFrame(), CurrentFrame(), frameInputHeld);
	gpGraphics->RenderMainImagePresentAcquire(NextFrame());

	// Quicksave
	Quicksave(rMenuInput);

	// Controller vibration
	float fVibration = std::pow(CurrentFrame().interpolate.camera.fCameraShake, 0.5f);
	gpRawInputManager->SetVibration(0, fVibration, fVibration);
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

void GameBase::SyncReplay(game::FrameInputHeld& rFrameInputHeld, game::FrameInputPressed& rFrameInputPressed)
{
#if defined(ENABLE_DEBUG_INPUT)
	if (mbSaveReplay && mpDifferenceStreamWriterHeld == nullptr)
	{
		mbSaveReplay = false;

		mpDifferenceStreamReaderHeld.reset();
		mpDifferenceStreamReaderPressed.reset();

		LOG("Start recording replay at {}", CurrentFrame().camera.iFrame);
		mpDifferenceStreamWriterHeld = std::make_unique<engine::DifferenceStreamWriter<game::Frame, game::FrameInputHeld>>(CurrentFrame(), rFrameInputHeld);
		mpDifferenceStreamWriterPressed = std::make_unique<engine::DifferenceStreamWriter<game::Frame, game::FrameInputPressed>>(CurrentFrame(), rFrameInputPressed);

		return;
	}
	else if (mbSaveReplay && mpDifferenceStreamWriterHeld != nullptr)
	{
		mbSaveReplay = false;

		LOG("Saving replay at {}", CurrentFrame().camera.iFrame);
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

			mpDifferenceStreamWriterHeld.reset();
			mpDifferenceStreamWriterPressed.reset();
			mpDifferenceStreamReaderHeld = std::make_unique<engine::DifferenceStreamReader<game::Frame, game::FrameInputHeld>>(engine::FileFlags_t {FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7Held.replay"), CurrentFrame(), rFrameInputHeld);
			mpDifferenceStreamReaderPressed = std::make_unique<engine::DifferenceStreamReader<game::Frame, game::FrameInputPressed>>(engine::FileFlags_t {FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("F7Pressed.replay"), CurrentFrame(), rFrameInputPressed);
			// DT: TEMP Not needed? memcpy(&NextFrame(), &CurrentFrame(), sizeof(NextFrame()));

			if (mpDifferenceStreamReaderHeld->Loaded() && mpDifferenceStreamReaderPressed->Loaded())
			{
				LOG("Loaded replay at {}", CurrentFrame().camera.iFrame);
			}
			else
			{
				mpDifferenceStreamReaderHeld.reset();
				mpDifferenceStreamReaderPressed.reset();
			}

			return;
		}
	}

	UpdateDifferenceStream(CurrentFrame().camera.iFrame, rFrameInputHeld, true, mpDifferenceStreamWriterHeld, mpDifferenceStreamReaderHeld, "held");
	UpdateDifferenceStream(CurrentFrame().camera.iFrame, rFrameInputPressed, true, mpDifferenceStreamWriterPressed, mpDifferenceStreamReaderPressed, "pressed");
#endif
}

void GameBase::LoadFromReplayHeld([[maybe_unused]] game::FrameInputHeld& rFrameInputHeld)
{
#if defined(ENABLE_DEBUG_INPUT)
	if (mpDifferenceStreamReaderHeld != nullptr) [[unlikely]]
	{
		mpDifferenceStreamReaderHeld->Update(CurrentFrame().camera.iFrame, rFrameInputHeld, false);
	}
#endif
}

} // namespace engine
