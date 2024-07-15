#include "GameBase.h"

#include "Audio/AudioManager.h"
#include "Graphics/Graphics.h"

#include "Frame/Frame.h"
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

void GameBase::PreInputUpdate()
{
	if (!ShouldUpdateFrame()) [[unlikely]]
	{
		gpGraphics->RenderGlobal(CurrentFrame());
		return;
	}

	// Estimate elapsed time (will be used to position visible area)
	std::chrono::nanoseconds realDeltaNs = mRealTime.GetDeltaNs(false);
	std::chrono::nanoseconds estimatedDeltaNs = mUpdateRemainderNs + (realDeltaNs * miTimeMultiply) / miTimeDivide;

	// Use global frame as a temporary frame to estimate visible area position, then use that to start global render
	UpdateFrameBase(NextFrame(), CurrentFrame(), game::FrameInput(), common::NanosecondsToFloatSeconds<float>(estimatedDeltaNs), FrameType::kGlobal);
	gpGraphics->RenderGlobal(NextFrame());
}

bool GameBase::Update(bool bSingleStep, bool bLostFocus, game::FrameInput& rFrameInput)
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

	common::Timer updateTimer;
	for (int64_t i = 0; i < iUpdates; ++i)
	{
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
		UpdateFrameBase(NextFrame(), CurrentFrame(), rFrameInput, kfDeltaTime, FrameType::kFull);
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

	// Use temporary frame, interpolate positions and rotations for render
	UpdateFrameBase(NextFrame(), CurrentFrame(), rFrameInput, common::NanosecondsToFloatSeconds<float>(mUpdateRemainderNs), FrameType::kMain);
	gpGraphics->RenderMainImagePresentAcquire(NextFrame());

	return true;
}

} // namespace engine
