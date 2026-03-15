#include "TimeStep.h"

#include "Profile/ProfileManager.h"
#if defined(BT_CLIENT)
#include "Graphics/Managers/TextManager.h"
#endif

#include "Game.h"

namespace engine
{

int64_t TimeStep::TickRealtime()
{
	std::chrono::nanoseconds realDeltaNs = mRealTime.GetDeltaNs(true);

	if constexpr (kbEnableProfilingFrameSpike)
	{
		// Track delta for performance monitoring
		float fDelta = common::NanosecondsToFloatSeconds<float>(realDeltaNs);
		if (mAverageDelta.miCount > 200 && fDelta > 1.9f * mAverageDelta.Average())
		{
			Log("\n\n\n  deltaNs spike {} > {}", fDelta, mAverageDelta.Average());
			static bool sbOnce = false;
			if (!sbOnce)
			{
				sbOnce = true;
				gpProfileManager->LogTimers();
			}
		}
		Log("\n\n");

		mAverageDelta = fDelta;
	}

	// Accumulate time with scaling
	if constexpr (kbEnableDebugInput)
	{
		mTickRemainderNs += (realDeltaNs * miTimeMultiply) / miTimeDivide;

		// Death spiral prevention: detect excessive updates and auto-reduce time scale
		int64_t iEstimatedTicks = mTickRemainderNs / game::kTickNs;
		if (iEstimatedTicks > kiMaxTicksPerFrame && miTimeMultiply > 1) [[unlikely]]
		{
			Log("Death spiral detected: {} ticks at {}x speed", iEstimatedTicks, miTimeMultiply);
			DecreaseTimeScale(false);

			// Clamp accumulator to prevent backlog cascade
			std::chrono::nanoseconds maxAccumulator = game::kTickNs * kiMaxAccumulatorTicks;
			if (mTickRemainderNs > maxAccumulator)
			{
				mTickRemainderNs = maxAccumulator;
			}
		}
	}
	else
	{
		mTickRemainderNs += (realDeltaNs * miTimeMultiply) / miTimeDivide;

		// Death spiral prevention: clamp accumulator to prevent backlog cascade
		std::chrono::nanoseconds maxAccumulator = game::kTickNs * kiMaxAccumulatorTicks;
		if (mTickRemainderNs > maxAccumulator)
		{
			mTickRemainderNs = maxAccumulator;
		}
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
}

bool TimeStep::DecreaseTimeScale(bool bAllowSlowMo)
{
	if (miTimeMultiply > 1)
	{
		miTimeMultiply /= 2;
		Log("Time ratio: {}x", miTimeMultiply);
		if (miTimeMultiply == 1 && miTimeDivide == 1)
		{
#if defined(BT_CLIENT)
			gpTextManager->UpdateTextArea(kTextDebug, "");
#endif
		}
		else
		{
			common::gpThreadLocal->mWorkbuffer.Push();
			common::gpThreadLocal->mWorkbuffer.Append("Time ratio: ");
			common::gpThreadLocal->mWorkbuffer.Append(miTimeMultiply);
			common::gpThreadLocal->mWorkbuffer.Append("x");
#if defined(BT_CLIENT)
			gpTextManager->UpdateTextArea(kTextDebug, common::gpThreadLocal->mWorkbuffer.View());
#endif
			common::gpThreadLocal->mWorkbuffer.Pop();
		}
		return true;
	}
	else if (bAllowSlowMo)
	{
		miTimeDivide *= 2;
		Log("Time ratio: 1/{}x", miTimeDivide);
		common::gpThreadLocal->mWorkbuffer.Push();
		common::gpThreadLocal->mWorkbuffer.Append("Time ratio: 1/");
		common::gpThreadLocal->mWorkbuffer.Append(miTimeDivide);
		common::gpThreadLocal->mWorkbuffer.Append("x");
#if defined(BT_CLIENT)
		gpTextManager->UpdateTextArea(kTextDebug, common::gpThreadLocal->mWorkbuffer.View());
#endif
		common::gpThreadLocal->mWorkbuffer.Pop();
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
#if defined(BT_CLIENT)
			gpTextManager->UpdateTextArea(kTextDebug, "");
#endif
		}
		else
		{
			common::gpThreadLocal->mWorkbuffer.Push();
			common::gpThreadLocal->mWorkbuffer.Append("Time ratio: 1/");
			common::gpThreadLocal->mWorkbuffer.Append(miTimeDivide);
			common::gpThreadLocal->mWorkbuffer.Append("x");
#if defined(BT_CLIENT)
			gpTextManager->UpdateTextArea(kTextDebug, common::gpThreadLocal->mWorkbuffer.View());
#endif
			common::gpThreadLocal->mWorkbuffer.Pop();
		}
	}
	else
	{
		miTimeMultiply *= 2;
		Log("Time ratio: {}x", miTimeMultiply);
		common::gpThreadLocal->mWorkbuffer.Push();
		common::gpThreadLocal->mWorkbuffer.Append("Time ratio: ");
		common::gpThreadLocal->mWorkbuffer.Append(miTimeMultiply);
		common::gpThreadLocal->mWorkbuffer.Append("x");
#if defined(BT_CLIENT)
		gpTextManager->UpdateTextArea(kTextDebug, common::gpThreadLocal->mWorkbuffer.View());
#endif
		common::gpThreadLocal->mWorkbuffer.Pop();
	}
}

} // namespace engine
