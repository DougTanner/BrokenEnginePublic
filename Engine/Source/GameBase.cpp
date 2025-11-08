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
	mRealTime.Reset();
	gpAudioManager->mRealTime.Reset();
}

bool GameBase::Update(bool bSingleStep, bool bLostFocus, const engine::RawInput& rRawInput, game::MenuInput& rMenuInput, game::FrameInput& rFrameInput)
{
	if (bLostFocus) [[unlikely]]
	{
		ResetRealTime();
		mAverageDelta.miCount = 0;
	}

	std::chrono::nanoseconds realDeltaNs = mRealTime.GetDeltaNs(true);
	float fDelta = common::NanosecondsToFloatSeconds<float>(realDeltaNs);
	if (mAverageDelta.miCount > 200 && fDelta > 1.9f * mAverageDelta.Average())
	{
		LOG("\n\n\n  deltaNs spike {} > {}", fDelta, mAverageDelta.Average());
	#if defined(ENABLE_PROFILING)
		static bool sbOnce = false;
		if (!sbOnce)
		{
			sbOnce = true;
			gpProfileManager->LogTimers();
		}
	#endif
		LOG("\n\n");
	}
	mAverageDelta = fDelta;

	if (bSingleStep || bLostFocus) [[unlikely]]
	{
		mUpdateRemainderNs = kUpdateStepNs;
	}
	else [[likely]]
	{
		mUpdateRemainderNs += (realDeltaNs * miTimeMultiply) / miTimeDivide;
	}

	int64_t iUpdates = 0;
	while (mUpdateRemainderNs >= kUpdateStepNs)
	{
		mUpdateRemainderNs -= kUpdateStepNs;
		++iUpdates;
	}

	// Process input and start global render (happens once per frame before any physics updates)
	if (iUpdates > 0)
	{
		NextFrame().fDeltaTime = kfDeltaTime;
		rMenuInput = game::ProcessRawInput(rRawInput, rFrameInput);
		UpdateFrameGlobal(NextFrame(), CurrentFrame());
		CalculateMatricesAndVisibleArea(NextFrame(), true);
		game::CopyVisibleAreaToFrameInput(rFrameInput);
		gpGraphics->RenderGlobal(NextFrame());
	}

	common::Timer updateTimer;
	for (int64_t i = 0; i < iUpdates; ++i)
	{
		// First physics step already had UpdateFrameGlobal called above
		if (i > 0)
		{
			NextFrame().fDeltaTime = kfDeltaTime;
			UpdateFrameGlobal(NextFrame(), CurrentFrame());
		}

		if (mpDifferenceStreamWriter != nullptr) [[unlikely]]
		{
			mpDifferenceStreamWriter->Update(CurrentFrame().iFrame, rFrameInput);
		}

		if (mpDifferenceStreamReader != nullptr && !mpDifferenceStreamReader->Update(CurrentFrame().iFrame, rFrameInput)) [[unlikely]]
		{
			LOG("End replay at {}", CurrentFrame().iFrame);

			if constexpr (common::kbVerifyFrame)
			{
				bool bEqual = CurrentFrame() == mpDifferenceStreamReader->mHeader.savedEnd;
				if (!bEqual && CurrentFrame().player != mpDifferenceStreamReader->mHeader.savedEnd.player)
				{
					DEBUG_BREAK();
				}
				common::BreakOnNotEqual(bEqual);
			}

			EndReplay(rFrameInput);
		}

	#if defined(ENABLE_PROFILING)
		gpProfileManager->mUpdatesInTheLastSecond.Set();
	#endif
		// Complete frame update with interpolation and full physics
		UpdateFrameInterpolate(NextFrame(), CurrentFrame(), rFrameInput);
		UpdateFrameFull(NextFrame(), CurrentFrame(), rFrameInput);
		rFrameInput.pressedFlags.ClearAll();
		std::swap(mpCurrentFrame, mpNextFrame);

		std::chrono::nanoseconds monitorRefreshTimeNs = 1'000'000'000ns / gpGraphics->miMonitorRefreshRate;
		if (updateTimer.GetDeltaNs() > monitorRefreshTimeNs)
		{
			// Slow down simulation if it means simulation will cause us to miss VSync
			LOG("Slowing down simulation, deltaNs: {} ({}) iUpdates: {}", updateTimer.GetDeltaNs(), monitorRefreshTimeNs, iUpdates);
			if (miTimeMultiply > 1)
			{
				miTimeMultiply /= 2;
				LOG("Time ratio: {}x", miTimeMultiply);
				gpTextManager->UpdateTextArea(kTextDebug, std::string("Time ratio: ") + std::to_string(miTimeMultiply) + "x");
			}
			mUpdateRemainderNs = 0ns;
			gpGraphics->RenderMainImagePresentAcquire(CurrentFrame());
			return true;
		}
	}

	// Create interpolated frame for smooth rendering between physics steps
	// If no physics updates occurred, process input and render global first
	if (iUpdates == 0)
	{
		NextFrame().fDeltaTime = common::NanosecondsToFloatSeconds<float>(mUpdateRemainderNs);
		rMenuInput = game::ProcessRawInput(rRawInput, rFrameInput);
		UpdateFrameGlobal(NextFrame(), CurrentFrame());
		CalculateMatricesAndVisibleArea(NextFrame(), true);
		game::CopyVisibleAreaToFrameInput(rFrameInput);
		gpGraphics->RenderGlobal(NextFrame());
	}
	else
	{
		NextFrame().fDeltaTime = common::NanosecondsToFloatSeconds<float>(mUpdateRemainderNs);
		UpdateFrameGlobal(NextFrame(), CurrentFrame());
	}
	UpdateFrameInterpolate(NextFrame(), CurrentFrame(), rFrameInput);
	gpGraphics->RenderMainImagePresentAcquire(NextFrame());

	return true;
}

} // namespace engine
