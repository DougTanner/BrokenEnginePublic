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

void GameBase::UpdateFramesAndRender(game::FrameInput& rFrameInput, bool bLostFocus)
{
	if (mbQuit) [[unlikely]]
	{
		return;
	}

	int64_t iFullUpdates = mTimeStep.UpdateRealtime(bLostFocus);

	for (int64_t i = 0; i < iFullUpdates; ++i)
	{
		// Load or save input stream if active
		HandleReplay(CurrentFrame().global.iFrame + 1, NextFrame(), rFrameInput);

		// Do a full frame update
		WriteFrameGlobalBase(NextFrame(), CurrentFrame(), game::kfDeltaTime);
		WriteFrameInterpolateBase(NextFrame(), CurrentFrame(), rFrameInput);
		WriteFrameFullBase(NextFrame(), CurrentFrame(), rFrameInput);
		std::swap(mpCurrentFrame, mpNextFrame);

		// Button press should only be seen for a single frame
		rFrameInput.pressedFlags.ClearAll();
	}

	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(mTimeStep.mUpdateRemainderNs);
	if (fDeltaTime <= kfEpsilon)
	{
		gpGraphics->RenderPresentAcquire(CurrentFrame());
		return;
	}

	// Update global frame and trigger global rendering
	WriteFrameGlobalBase(NextFrame(), CurrentFrame(), fDeltaTime);
	gpGraphics->RenderGlobal(NextFrame());

	// Create an interpolated frame for final rendering
	WriteFrameInterpolateBase(NextFrame(), CurrentFrame(), rFrameInput);
	gpGraphics->RenderMainImagePresentAcquire(NextFrame());

	// Controller vibration
	float fVibration = std::pow(CurrentFrame().interpolate.camera.fCameraShake, 0.5f);
	gpRawInputManager->SetVibration(0, fVibration, fVibration);
}

void GameBase::HandleReplay(int64_t iFrame, const game::Frame& rFrame, game::FrameInput& rFrameInput)
{
	if (mpDifferenceStreamWriter != nullptr) [[unlikely]]
	{
		mpDifferenceStreamWriter->Update(iFrame, rFrameInput);
	}

	if (mpDifferenceStreamReader != nullptr) [[unlikely]]
	{
		if (!mpDifferenceStreamReader->Update(iFrame, rFrameInput))
		{
			LOG("End replay at {}", iFrame);
			common::BreakOnNotEqual(rFrame != mpDifferenceStreamReader->mHeader.savedEnd);
		}
	}
}

} // namespace engine
