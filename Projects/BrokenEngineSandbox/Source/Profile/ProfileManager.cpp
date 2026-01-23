#include "ProfileManager.h"

namespace game
{

ProfileManager* gpProfileManager = nullptr;

ProfileManager::ProfileManager()
: ProfileManagerBase()
{
	gpProfileManager = this;

	if constexpr (kbEnableProfiling)
	{
		BootStart(engine::kBootTimerTotal);
	}
}

ProfileManager::~ProfileManager()
{
	gpProfileManager = nullptr;
}

engine::CpuCounter& ProfileManager::GetCpuCounter(int64_t iIndex)
{
	return iIndex < engine::kEngineCpuCounterCount ? mEngineCpuCounters[iIndex] : mGameCpuCounters[iIndex - engine::kEngineCpuCounterCount];
}

engine::CpuTimer& ProfileManager::GetCpuTimer(int64_t iIndex)
{
	return iIndex < engine::kEngineCpuTimerCount ? mEngineCpuTimers[iIndex] : mGameCpuTimers[iIndex - engine::kEngineCpuTimerCount];
}

int64_t ProfileManager::GetCpuCounterCount() const
{
	return kGameCpuCounterCount;
}

int64_t ProfileManager::GetCpuTimerCount() const
{
	return kGameCpuTimerCount;
}

} // namespace game
