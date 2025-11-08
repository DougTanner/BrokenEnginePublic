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

bool GameBase::Update(bool bSingleStep, bool bLostFocus, const engine::RawInput& rRawInput, game::MenuInput& rMenuInput, game::FrameInput& rFrameInput)
{
	if (bLostFocus) [[unlikely]]
	{
		ResetRealTime();
	}

	std::chrono::nanoseconds realDeltaNs = mTimeStep.mRealTime.GetDeltaNs(true);
	int64_t iUpdates = mTimeStep.AddDelta(realDeltaNs, bSingleStep, bLostFocus);

	UpdatePhysicsSteps(iUpdates, rRawInput, rMenuInput, rFrameInput);

	CreateInterpolatedFrame(iUpdates, rRawInput, rMenuInput, rFrameInput);

	return true;
}

void GameBase::UpdatePhysicsSteps(int64_t iUpdates, const engine::RawInput& rRawInput, game::MenuInput& rMenuInput, game::FrameInput& rFrameInput)
{
	// Process input and start global render (happens once per frame before any physics updates)
	if (iUpdates > 0)
	{
		NextFrame().global.fDeltaTime = kfDeltaTime;
		rMenuInput = game::ProcessRawInput(rRawInput, rFrameInput);
		UpdateFrameGlobal(NextFrame(), CurrentFrame());
		CalculateMatricesAndVisibleArea(NextFrame(), true);
		game::CopyVisibleAreaToFrameInput(rFrameInput);
		gpGraphics->RenderGlobal(NextFrame());
	}

	common::Timer updateTimer;
	for (int64_t i = 0; i < iUpdates; ++i)
	{
		bool bFirstStep = (i == 0);
		UpdateSinglePhysicsStep(bFirstStep, rFrameInput);

		std::chrono::nanoseconds monitorRefreshTimeNs = 1'000'000'000ns / gpGraphics->miMonitorRefreshRate;
		if (updateTimer.GetDeltaNs() > monitorRefreshTimeNs)
		{
			// Slow down simulation if it means simulation will cause us to miss VSync
			LOG("Slowing down simulation, deltaNs: {} ({}) iUpdates: {}", updateTimer.GetDeltaNs(), monitorRefreshTimeNs, iUpdates);
			if (mTimeStep.AdjustTimeScale(monitorRefreshTimeNs))
			{
				gpTextManager->UpdateTextArea(kTextDebug, std::string("Time ratio: ") + std::to_string(mTimeStep.GetTimeMultiplier()) + "x");
			}
			mTimeStep.ClearAccumulator();
			gpGraphics->RenderMainImagePresentAcquire(CurrentFrame());
			return;
		}
	}
}

void GameBase::UpdateSinglePhysicsStep(bool bFirstStep, game::FrameInput& rFrameInput)
{
	// First physics step already had UpdateFrameGlobal called above
	if (!bFirstStep)
	{
		NextFrame().global.fDeltaTime = kfDeltaTime;
		UpdateFrameGlobal(NextFrame(), CurrentFrame());
	}

	HandleReplay(rFrameInput);

#if defined(ENABLE_PROFILING)
	gpProfileManager->mUpdatesInTheLastSecond.Set();
#endif

	// Complete frame update with interpolation and full physics
	UpdateFrameInterpolate(NextFrame(), CurrentFrame(), rFrameInput);
	UpdateFrameFull(NextFrame(), CurrentFrame(), rFrameInput);

	SwapFrames(rFrameInput);
}

void GameBase::HandleReplay(game::FrameInput& rFrameInput)
{
	if (mpDifferenceStreamWriter != nullptr) [[unlikely]]
	{
		mpDifferenceStreamWriter->Update(CurrentFrame().global.iFrame, rFrameInput);
	}

	if (mpDifferenceStreamReader != nullptr && !mpDifferenceStreamReader->Update(CurrentFrame().global.iFrame, rFrameInput)) [[unlikely]]
	{
		LOG("End replay at {}", CurrentFrame().global.iFrame);

		if constexpr (common::kbVerifyFrame)
		{
			bool bEqual = CurrentFrame() == mpDifferenceStreamReader->mHeader.savedEnd;
			if (!bEqual && CurrentFrame().interpolate.player != mpDifferenceStreamReader->mHeader.savedEnd.interpolate.player)
			{
				DEBUG_BREAK();
			}
			common::BreakOnNotEqual(bEqual);
		}

		EndReplay(rFrameInput);
	}
}

void GameBase::SwapFrames(game::FrameInput& rFrameInput)
{
	rFrameInput.pressedFlags.ClearAll();
	std::swap(mpCurrentFrame, mpNextFrame);
}

void GameBase::CreateInterpolatedFrame(int64_t iUpdates, const engine::RawInput& rRawInput, game::MenuInput& rMenuInput, game::FrameInput& rFrameInput)
{
	// Create interpolated frame for smooth rendering between physics steps
	// If no physics updates occurred, process input and render global first
	if (iUpdates == 0)
	{
		NextFrame().global.fDeltaTime = common::NanosecondsToFloatSeconds<float>(mTimeStep.mUpdateRemainderNs);
		rMenuInput = game::ProcessRawInput(rRawInput, rFrameInput);
		UpdateFrameGlobal(NextFrame(), CurrentFrame());
		CalculateMatricesAndVisibleArea(NextFrame(), true);
		game::CopyVisibleAreaToFrameInput(rFrameInput);
		gpGraphics->RenderGlobal(NextFrame());
	}
	else
	{
		NextFrame().global.fDeltaTime = common::NanosecondsToFloatSeconds<float>(mTimeStep.mUpdateRemainderNs);
		UpdateFrameGlobal(NextFrame(), CurrentFrame());
	}
	UpdateFrameInterpolate(NextFrame(), CurrentFrame(), rFrameInput);
	gpGraphics->RenderMainImagePresentAcquire(NextFrame());
}

} // namespace engine
