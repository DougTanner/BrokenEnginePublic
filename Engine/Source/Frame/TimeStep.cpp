#include "TimeStep.h"

#if defined(ENABLE_PROFILING)
#include "Profile/ProfileManager.h"
#endif

namespace engine
{

TimeStep::TimeStep()
{
}

void TimeStep::Reset()
{
	mRealTime.Reset();
	mAverageDelta.miCount = 0;
}

int64_t TimeStep::AddDelta(std::chrono::nanoseconds realDeltaNs, bool bSingleStep, bool bLostFocus)
{
	// Track delta for performance monitoring
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

	// Accumulate time with scaling
	if (bSingleStep || bLostFocus) [[unlikely]]
	{
		mUpdateRemainderNs = kUpdateStepNs;
	}
	else [[likely]]
	{
		mUpdateRemainderNs += (realDeltaNs * miTimeMultiply) / miTimeDivide;
	}

	// Calculate number of physics steps needed
	int64_t iUpdates = 0;
	while (mUpdateRemainderNs >= kUpdateStepNs)
	{
		mUpdateRemainderNs -= kUpdateStepNs;
		++iUpdates;
	}

	return iUpdates;
}

void TimeStep::ConsumeStep()
{
	// Step is consumed by AddDelta, nothing to do here
	// This method exists for future extensions or manual control
}

float TimeStep::GetInterpolationAlpha() const
{
	return common::NanosecondsToFloatSeconds<float>(mUpdateRemainderNs) / common::NanosecondsToFloatSeconds<float>(kUpdateStepNs);
}

bool TimeStep::AdjustTimeScale(std::chrono::nanoseconds monitorRefreshTimeNs)
{
	if (miTimeMultiply > 1)
	{
		miTimeMultiply /= 2;
		LOG("Time ratio: {}x", miTimeMultiply);
		return true;
	}
	return false;
}

void TimeStep::ClearAccumulator()
{
	mUpdateRemainderNs = 0ns;
}

void TimeStep::SetTimeScale(int64_t iMultiply, int64_t iDivide)
{
	miTimeMultiply = iMultiply;
	miTimeDivide = iDivide;
}

} // namespace engine
