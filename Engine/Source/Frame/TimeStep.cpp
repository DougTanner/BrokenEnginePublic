#include "TimeStep.h"

#include "Profile/ProfileManager.h"

#include "Game.h"

namespace engine
{

int64_t TimeStep::TickRealtime()
{
	std::chrono::nanoseconds realDeltaNs = mRealTime.GetDeltaNs(true);

	if constexpr (kbProfilingFrameSpike)
	{
		// Track delta for performance monitoring
		float fDelta = common::NanosecondsToFloatSeconds<float>(realDeltaNs);
		if (mAverageDelta.miCount > 200 && fDelta > 1.9f * mAverageDelta.Average())
		{
			Log(kWarning, "\n\n\n  deltaNs spike {} > {}", fDelta, mAverageDelta.Average());
			static bool sbOnce = false;
			if (!sbOnce)
			{
				sbOnce = true;
				gpProfileManager->LogTimers();
			}
		}
		Log(kVerbose, "\n\n");

		mAverageDelta = fDelta;
	}

	// Accumulate time with scaling
	mTickRemainderNs += (realDeltaNs * miTimeMultiply) / miTimeDivide;

	// Death spiral prevention: detect excessive updates and auto-reduce time scale
	if constexpr (kbDebugInput)
	{
		int64_t iEstimatedTicks = mTickRemainderNs / game::kTickNs;
		if (iEstimatedTicks > kiMaxTicksPerFrame && miTimeMultiply > 1) [[unlikely]]
		{
			Log(kWarning, "Death spiral detected: {} ticks at {}x speed", iEstimatedTicks, miTimeMultiply);
			DecreaseTimeScale(false);
		}
	}

	// Clamp accumulator to prevent backlog cascade (e.g., after background/focus loss)
	int64_t iEffectiveMaxTicks = (miCatchUpAccumulatorTicks > 0) ? miCatchUpAccumulatorTicks : kiMaxAccumulatorTicks;
	std::chrono::nanoseconds maxAccumulator = game::kTickNs * iEffectiveMaxTicks;
	if (mTickRemainderNs > maxAccumulator)
	{
		Log(kVerbose, "TimeStep::TickRealtime Accumulator clamped");
		mTickRemainderNs = maxAccumulator;
	}

	// Calculate number of ticks needed
	int64_t iTicks = mTickRemainderNs / game::kTickNs;
	mTickRemainderNs %= game::kTickNs;

	return iTicks;
}

float TimeStep::GetInterpolationAlpha() const
{
	return static_cast<float>(mTickRemainderNs.count()) / static_cast<float>(game::kTickNs.count());
}

void TimeStep::ClearAccumulator()
{
	mTickRemainderNs = 0ns;
}

void TimeStep::SetTimeScale(int64_t iMultiply, int64_t iDivide)
{
	miTimeMultiply = iMultiply;
	miTimeDivide = iDivide;
	mbTimeScaleChanged = true;
}

bool TimeStep::DecreaseTimeScale(bool bAllowSlowMo)
{
	if (miTimeMultiply > 1)
	{
		miTimeMultiply /= 2;
		Log("Time ratio: {}x", miTimeMultiply);
		mbTimeScaleChanged = true;
		return true;
	}
	else if (bAllowSlowMo)
	{
		miTimeDivide *= 2;
		Log("Time ratio: 1/{}x", miTimeDivide);
		mbTimeScaleChanged = true;
		return true;
	}
	return false;
}

void TimeStep::IncreaseTimeScale()
{
	if (miTimeDivide > 1)
	{
		miTimeDivide /= 2;
		Log("Time ratio: 1/{}x", miTimeDivide);
	}
	else
	{
		miTimeMultiply *= 2;
		Log("Time ratio: {}x", miTimeMultiply);
	}
	mbTimeScaleChanged = true;
}

} // namespace engine
