#include "TimeStep.h"

#include "Frame/Frame.h"
#include "Profile/ProfileManager.h"

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
			LOG(kDefault, kWarning, "\n\n\n  deltaNs spike {} > {}", common::Wb(fDelta, 4), common::Wb(mAverageDelta.Average(), 4));
			static bool sbOnce = false;
			if (!sbOnce)
			{
				sbOnce = true;
				gpProfileManager->LogTimers();
			}
		}
		LOG(kDefault, kVerbose, "\n\n");

		mAverageDelta = fDelta;
	}

	// Accumulate time with scaling
	mTickRemainderNs += WallToSim(realDeltaNs);

	// Death spiral prevention: detect excessive updates and auto-reduce time scale
	if constexpr (kbDebugInput)
	{
		int64_t iEstimatedTicks = mTickRemainderNs / game::kTickNs;
		if (iEstimatedTicks > kiMaxTicksPerFrame && miTimeMultiply > 1) [[unlikely]]
		{
			LOG(kDefault, kWarning, "Death spiral detected: {} ticks at {}x speed", iEstimatedTicks, miTimeMultiply);
			DecreaseTimeScale(false);
		}
	}

	// Clamp accumulator to prevent backlog cascade (e.g., after background/focus loss)
	std::chrono::nanoseconds maxAccumulator = game::kTickNs * kiMaxAccumulatorTicks;
	if (mTickRemainderNs > maxAccumulator)
	{
		LOG(kDefault, kVerbose, "TimeStep::TickRealtime Accumulator clamped");
		mTickRemainderNs = maxAccumulator;
	}

	// Calculate number of ticks needed
	int64_t iTicks = mTickRemainderNs / game::kTickNs;
	mTickRemainderNs %= game::kTickNs;

	return iTicks;
}

void TimeStep::ClearAccumulator()
{
	mTickRemainderNs = 0ns;
}

void TimeStep::AbsorbUnusedTicks(int64_t iTicks)
{
	mTickRemainderNs += iTicks * game::kTickNs;
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
		LOG(kDefault, kDebug, "Time ratio: {}x", miTimeMultiply);
		mbTimeScaleChanged = true;
		return true;
	}
	else if (bAllowSlowMo)
	{
		miTimeDivide *= 2;
		LOG(kDefault, kDebug, "Time ratio: 1/{}x", miTimeDivide);
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
		LOG(kDefault, kDebug, "Time ratio: 1/{}x", miTimeDivide);
	}
	else
	{
		miTimeMultiply *= 2;
		LOG(kDefault, kDebug, "Time ratio: {}x", miTimeMultiply);
	}
	mbTimeScaleChanged = true;
}

} // namespace engine
