#include "TimeStep.h"

#include "Profile/ProfileManager.h"
#include "ThreadLocal.h"

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
		Log("\n\n\n  deltaNs spike {} > {}", fDelta, mAverageDelta.Average());
		if constexpr (kbEnableProfiling)
		{
			static bool sbOnce = false;
			if (!sbOnce)
			{
				sbOnce = true;
				gpProfileManager->LogTimers();
			}
		}
		Log("\n\n");
	}
	mAverageDelta = fDelta;

	// Accumulate time with scaling
	if constexpr (kbEnableDebugInput)
	{
		if (mbSingleStep || bLostFocus) [[unlikely]]
		{
			mbSingleStep = false;
			mUpdateRemainderNs = game::kUpdateStepNs;
		}
		else [[likely]]
		{
			mUpdateRemainderNs += (realDeltaNs * miTimeMultiply) / miTimeDivide;
		}

		// Death spiral prevention: detect excessive updates and auto-reduce time scale
		int64_t iEstimatedUpdates = mUpdateRemainderNs / game::kUpdateStepNs;
		if (iEstimatedUpdates > kiMaxUpdatesPerFrame && miTimeMultiply > 1) [[unlikely]]
		{
			Log("Death spiral detected: {} updates at {}x speed", iEstimatedUpdates, miTimeMultiply);
			DecreaseTimeScale(false);

			// Clamp accumulator to prevent backlog cascade
			std::chrono::nanoseconds maxAccumulator = game::kUpdateStepNs * kiMaxAccumulatorSteps;
			if (mUpdateRemainderNs > maxAccumulator)
			{
				mUpdateRemainderNs = maxAccumulator;
			}
		}
	}
	else
	{
		if (bLostFocus) [[unlikely]]
		{
			mUpdateRemainderNs = game::kUpdateStepNs;
		}
		else [[likely]]
		{
			mUpdateRemainderNs += (realDeltaNs * miTimeMultiply) / miTimeDivide;
		}
	}

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

bool TimeStep::DecreaseTimeScale(bool bAllowSlowMo)
{
	if (miTimeMultiply > 1)
	{
		miTimeMultiply /= 2;
		Log("Time ratio: {}x", miTimeMultiply);
		if (miTimeMultiply == 1 && miTimeDivide == 1)
		{
			gpTextManager->UpdateTextArea(kTextDebug, "");
		}
		else
		{
			common::gpThreadLocal->mWorkbuffer.Clear();
			common::gpThreadLocal->mWorkbuffer.Append("Time ratio: ");
			common::gpThreadLocal->mWorkbuffer.Append(miTimeMultiply);
			common::gpThreadLocal->mWorkbuffer.Append("x");
			gpTextManager->UpdateTextArea(kTextDebug, common::gpThreadLocal->mWorkbuffer.View());
		}
		return true;
	}
	else if (bAllowSlowMo)
	{
		miTimeDivide *= 2;
		Log("Time ratio: 1/{}x", miTimeDivide);
		common::gpThreadLocal->mWorkbuffer.Clear();
		common::gpThreadLocal->mWorkbuffer.Append("Time ratio: 1/");
		common::gpThreadLocal->mWorkbuffer.Append(miTimeDivide);
		common::gpThreadLocal->mWorkbuffer.Append("x");
		gpTextManager->UpdateTextArea(kTextDebug, common::gpThreadLocal->mWorkbuffer.View());
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
		if (miTimeDivide == 1 && miTimeMultiply == 1)
		{
			gpTextManager->UpdateTextArea(kTextDebug, "");
		}
		else
		{
			common::gpThreadLocal->mWorkbuffer.Clear();
			common::gpThreadLocal->mWorkbuffer.Append("Time ratio: 1/");
			common::gpThreadLocal->mWorkbuffer.Append(miTimeDivide);
			common::gpThreadLocal->mWorkbuffer.Append("x");
			gpTextManager->UpdateTextArea(kTextDebug, common::gpThreadLocal->mWorkbuffer.View());
		}
	}
	else
	{
		miTimeMultiply *= 2;
		Log("Time ratio: {}x", miTimeMultiply);
		common::gpThreadLocal->mWorkbuffer.Clear();
		common::gpThreadLocal->mWorkbuffer.Append("Time ratio: ");
		common::gpThreadLocal->mWorkbuffer.Append(miTimeMultiply);
		common::gpThreadLocal->mWorkbuffer.Append("x");
		gpTextManager->UpdateTextArea(kTextDebug, common::gpThreadLocal->mWorkbuffer.View());
	}
}

} // namespace engine
