#include "TimeStep.h"

#if defined(ENABLE_PROFILING)
#include "Profile/ProfileManager.h"
#endif

#include "Game.h"
#include "Graphics/Managers/TextManager.h"

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

int64_t TimeStep::UpdateRealtime(bool bLostFocus)
{
	std::chrono::nanoseconds realDeltaNs = mRealTime.GetDeltaNs(true);

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
#if defined(ENABLE_DEBUG_INPUT)
	if (mbSingleStep || bLostFocus) [[unlikely]]
#else
	if (bLostFocus) [[unlikely]]
#endif
	{
	#if defined(ENABLE_DEBUG_INPUT)
		mbSingleStep = false;
	#endif
		mUpdateRemainderNs = game::kUpdateStepNs;
	}
	else [[likely]]
	{
		mUpdateRemainderNs += (realDeltaNs * miTimeMultiply) / miTimeDivide;
	}

#if defined(ENABLE_DEBUG_INPUT)
	// Death spiral prevention: detect excessive updates and auto-reduce time scale
	int64_t iEstimatedUpdates = mUpdateRemainderNs / game::kUpdateStepNs;
	if (iEstimatedUpdates > kiMaxUpdatesPerFrame && miTimeMultiply > 1) [[unlikely]]
	{
		LOG("Death spiral detected: {} updates at {}x speed", iEstimatedUpdates, miTimeMultiply);
		DecreaseTimeScale(false);

		// Clamp accumulator to prevent backlog cascade
		std::chrono::nanoseconds maxAccumulator = game::kUpdateStepNs * kiMaxAccumulatorSteps;
		if (mUpdateRemainderNs > maxAccumulator)
		{
			mUpdateRemainderNs = maxAccumulator;
		}
	}
#endif

	// Calculate number updates needed
	int64_t iUpdates = 0;
	while (mUpdateRemainderNs >= game::kUpdateStepNs)
	{
		mUpdateRemainderNs -= game::kUpdateStepNs;
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
	return common::NanosecondsToFloatSeconds<float>(mUpdateRemainderNs) / common::NanosecondsToFloatSeconds<float>(game::kUpdateStepNs);
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

#if defined(ENABLE_DEBUG_INPUT)
bool TimeStep::DecreaseTimeScale(bool bAllowSlowMo)
{
	if (miTimeMultiply > 1)
	{
		miTimeMultiply /= 2;
		LOG("Time ratio: {}x", miTimeMultiply);
		if (miTimeMultiply == 1 && miTimeDivide == 1)
		{
			gpTextManager->UpdateTextArea(kTextDebug, "");
		}
		else
		{
			gpTextManager->UpdateTextArea(kTextDebug, std::string("Time ratio: ") + std::to_string(miTimeMultiply) + "x");
		}
		return true;
	}
	else if (bAllowSlowMo)
	{
		miTimeDivide *= 2;
		LOG("Time ratio: 1/{}x", miTimeDivide);
		gpTextManager->UpdateTextArea(kTextDebug, std::string("Time ratio: 1/") + std::to_string(miTimeDivide) + "x");
		return true;
	}
	return false;
}

void TimeStep::IncreaseTimeScale()
{
	if (miTimeDivide > 1)
	{
		miTimeDivide /= 2;
		LOG("Time ratio: 1/{}x", miTimeDivide);
		if (miTimeDivide == 1 && miTimeMultiply == 1)
		{
			gpTextManager->UpdateTextArea(kTextDebug, "");
		}
		else
		{
			gpTextManager->UpdateTextArea(kTextDebug, std::string("Time ratio: 1/") + std::to_string(miTimeDivide) + "x");
		}
	}
	else
	{
		miTimeMultiply *= 2;
		LOG("Time ratio: {}x", miTimeMultiply);
		gpTextManager->UpdateTextArea(kTextDebug, std::string("Time ratio: ") + std::to_string(miTimeMultiply) + "x");
	}
}
#endif

} // namespace engine
