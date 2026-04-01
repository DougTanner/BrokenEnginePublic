#include "Pch.h"

#include "ProfileManagerBase.h"

#include "Profile/ProfileManager.h"

namespace engine
{

#if defined(BT_CLIENT)
void FormatFpsHeader(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, int64_t iTotalCpuTimeUs);
void FormatCpuScreen(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager);
void FormatGpuScreen(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager);
#endif

ProfileManagerBase::ProfileManagerBase()
{
}

ProfileManagerBase::~ProfileManagerBase()
{
}

void ProfileManagerBase::Create()
{
	if constexpr (kbProfiling)
	{
#if defined(BT_CLIENT)
		if (mVkQueryPool != VK_NULL_HANDLE)
		{
			return;
		}

		// Validate that graphics queue family supports timestamp queries
		uint32_t uiTimestampValidBits = gpInstanceManager->mVkQueueFamilyProperties[gpInstanceManager->miGraphicsQueueFamilyIndex].timestampValidBits;
		if (uiTimestampValidBits == 0)
		{
			Log("Warning: Graphics queue family does not support timestamp queries. GPU profiling disabled.");
			return;
		}

		int64_t iQueryCount = gpSwapchainManager->mFramebuffers.size() * 2 * kGpuTimerCount;

		VkQueryPoolCreateInfo vkQueryPoolCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.queryType = VK_QUERY_TYPE_TIMESTAMP,
			.queryCount = static_cast<uint32_t>(iQueryCount),
			.pipelineStatistics = 0,
		};

		CHECK_VK(vkCreateQueryPool(gpDeviceManager->mVkDevice, &vkQueryPoolCreateInfo, nullptr, &mVkQueryPool));
		VkName(VK_OBJECT_TYPE_QUERY_POOL, mVkQueryPool, "Timestamp");

		// Initial reset of all queries before command buffer recording
		OneShotCommandBuffer oneShotCommandBuffer;
		vkCmdResetQueryPool(oneShotCommandBuffer.mVkCommandBuffer, mVkQueryPool, 0, static_cast<uint32_t>(iQueryCount));
		oneShotCommandBuffer.Execute(true);
#endif // BT_CLIENT
	}
}

void ProfileManagerBase::Destroy()
{
	if constexpr (kbProfiling)
	{
#if defined(BT_CLIENT)
		if (gpDeviceManager != nullptr && mVkQueryPool != VK_NULL_HANDLE)
		{
			vkDestroyQueryPool(gpDeviceManager->mVkDevice, mVkQueryPool, nullptr);
		}

		mVkQueryPool = VK_NULL_HANDLE;
#endif // BT_CLIENT
	}
}

void ProfileManagerBase::ToggleProfileText()
{
	if constexpr (kbProfiling)
	{
		meProfileScreen = static_cast<ProfileScreen>((static_cast<uint8_t>(meProfileScreen) + 1) % static_cast<uint8_t>(ProfileScreen::kCount));

#if defined(BT_CLIENT)
		for (int64_t i = kTextGraphics; i < kTextAreasCount; ++i)
		{
			gpTextManager->UpdateTextArea(static_cast<TextAreas>(i), "");
		}
#endif
	}
}

void ProfileManagerBase::CpuStart(int64_t iCpuTimer, int64_t iThreads)
{
	if constexpr (kbProfiling)
	{
		std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
		int64_t iAllocations = giAllocationsThisFrame.load(std::memory_order_relaxed);

		std::lock_guard lock(mCpuTimerMutex);

		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

		std::vector<CpuTimerThreadState>& rThreadStates = mPerThreadTimerStates.try_emplace(std::this_thread::get_id()).first->second;
		if (rThreadStates.size() < static_cast<size_t>(GetCpuTimerCount()))
		{
			rThreadStates.resize(static_cast<size_t>(GetCpuTimerCount()));
		}

		CpuTimerThreadState& rState = rThreadStates[static_cast<size_t>(iCpuTimer)];
		ASSERT(rState.startTimePoint == std::chrono::high_resolution_clock::time_point());
		rState.startTimePoint = now;
		rState.iStartAllocations = iAllocations;

		GetCpuTimer(iCpuTimer).iThreads = iThreads;
	}
}

void ProfileManagerBase::CpuStop(int64_t iCpuTimer, bool bSmoothNow, bool bCrossThread)
{
	if constexpr (kbProfiling)
	{
		std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
		int64_t iAllocations = giAllocationsThisFrame.load(std::memory_order_relaxed);

		std::lock_guard lock(mCpuTimerMutex);

		CpuTimerThreadState* pState = nullptr;

		if (bCrossThread)
		{
			// Search all threads for the one that started this timer
			for (auto& [rThreadId, rStates] : mPerThreadTimerStates)
			{
				if (rStates.size() > static_cast<size_t>(iCpuTimer) && rStates[static_cast<size_t>(iCpuTimer)].startTimePoint != std::chrono::high_resolution_clock::time_point())
				{
					pState = &rStates[static_cast<size_t>(iCpuTimer)];
					break;
				}
			}
		}
		else
		{
			// Same-thread start/stop: use current thread's state directly
			std::vector<CpuTimerThreadState>& rThreadStates = mPerThreadTimerStates.try_emplace(std::this_thread::get_id()).first->second;
			if (rThreadStates.size() < static_cast<size_t>(GetCpuTimerCount()))
			{
				ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
				rThreadStates.resize(static_cast<size_t>(GetCpuTimerCount()));
			}
			pState = &rThreadStates[static_cast<size_t>(iCpuTimer)];
			ASSERT(pState->startTimePoint != std::chrono::high_resolution_clock::time_point());
		}

		CpuTimer& rCpuTimer = GetCpuTimer(iCpuTimer);

		if (pState != nullptr) [[likely]]
		{
			rCpuTimer.iTotalFrameTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(now - pState->startTimePoint).count();
			pState->startTimePoint = std::chrono::high_resolution_clock::time_point();
			rCpuTimer.iAllocationsThisFrame += std::max(static_cast<int64_t>(0), iAllocations - pState->iStartAllocations);
		}

		if (bSmoothNow) [[unlikely]]
		{
			rCpuTimer.smoothedMicroseconds = rCpuTimer.iTotalFrameTimeNs / 1000;
			rCpuTimer.iTotalFrameTimeNs = 0;
			rCpuTimer.smoothedAllocations = rCpuTimer.iAllocationsThisFrame;
			rCpuTimer.iAllocationsThisFrame = 0;
		}
	}
}

void ProfileManagerBase::SetCount(int64_t iCounter, int64_t iCount)
{
	if constexpr (kbProfiling) { GetCpuCounter(iCounter).iCount = iCount; }
}

#if defined(BT_CLIENT)
void ProfileManagerBase::ResetQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eStart, GpuTimers eEnd)
{
	if constexpr (kbProfiling)
	{
		if (mVkQueryPool == VK_NULL_HANDLE)
		{
			return;
		}

		uint32_t uiIndex = static_cast<uint32_t>(2 * (kGpuTimerCount * iCommandBuffer + eStart));
		uint32_t uiCount = static_cast<uint32_t>(2 * (eEnd - eStart));
		vkCmdResetQueryPool(vkCommandBuffer, mVkQueryPool, uiIndex, uiCount);
	}
}
#endif // BT_CLIENT

#if defined(BT_CLIENT)
void ProfileManagerBase::GpuStart(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGpuTimer)
{
	if constexpr (kbProfiling)
	{
		if (mVkQueryPool == VK_NULL_HANDLE)
		{
			return;
		}

		uint32_t uiCounterIndex = static_cast<uint32_t>(2 * kGpuTimerCount * iCommandBuffer + 2 * eGpuTimer);
		vkCmdWriteTimestamp(vkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mVkQueryPool, uiCounterIndex);

		bool bRoot = eGpuTimer == kGpuTimerGlobal || eGpuTimer == kGpuTimerMain || eGpuTimer == kGpuTimerImage;
		if (vkCmdBeginDebugUtilsLabelEXT != nullptr)
		{
			float fColor = bRoot ? 0.5f : 0.0f;
			VkDebugUtilsLabelEXT vkDebugUtilsLabelEXT =
			{
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
				.pLabelName = mGpuTimers[eGpuTimer].name.data(),
				.color = {fColor, fColor, fColor, fColor},
			};
			vkCmdBeginDebugUtilsLabelEXT(vkCommandBuffer, &vkDebugUtilsLabelEXT);
		}
	}
}

void ProfileManagerBase::GpuStop(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGpuTimer)
{
	if constexpr (kbProfiling)
	{
		if (mVkQueryPool == VK_NULL_HANDLE)
		{
			return;
		}

		uint32_t uiCounterIndex = static_cast<uint32_t>(2 * kGpuTimerCount * iCommandBuffer + 2 * eGpuTimer + 1);
		vkCmdWriteTimestamp(vkCommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mVkQueryPool, uiCounterIndex);

		if (vkCmdEndDebugUtilsLabelEXT != nullptr)
		{
			vkCmdEndDebugUtilsLabelEXT(vkCommandBuffer);
		}
	}
}

void ProfileManagerBase::GpuRead(int64_t iCommandBuffer, GpuTimers eStart, GpuTimers eEnd)
{
	if constexpr (kbProfiling)
	{
		if (mVkQueryPool == VK_NULL_HANDLE)
		{
			return;
		}

		// Read query results without blocking - skip if not ready to prevent hangs at low framerates
		for (int64_t i = eStart; i < eEnd; ++i)
		{
			GpuTimers eGpuTimer = static_cast<GpuTimers>(i);
			uint64_t puiResults[2] {};
			uint32_t uiCounterIndex = static_cast<uint32_t>(2 * kGpuTimerCount * iCommandBuffer + 2 * eGpuTimer);
			VkResult vkResultGetQueryPoolResults = vkGetQueryPoolResults(gpDeviceManager->mVkDevice, mVkQueryPool, uiCounterIndex, 2, sizeof(puiResults), puiResults, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
			if (vkResultGetQueryPoolResults == VK_NOT_READY)
			{
				continue;
			}
			CHECK_VK(vkResultGetQueryPoolResults);

			// Convert timestamp units to microseconds using device-specific timestampPeriod
			mGpuTimers[eGpuTimer].smoothedMicroseconds = static_cast<int64_t>(static_cast<float>(puiResults[1] - puiResults[0]) * gpInstanceManager->mVkPhysicalDeviceProperties.limits.timestampPeriod / 1000.0f);
		}
	}
}
#endif // BT_CLIENT

void ProfileManagerBase::BootStart(BootTimers eBootTimer)
{
	if constexpr (kbProfiling)
	{
		mBootTimers[eBootTimer].startTimePoint = std::chrono::high_resolution_clock::now();
	}
}

void ProfileManagerBase::BootStop(BootTimers eBootTimer)
{
	if constexpr (kbProfiling)
	{
		BootTimer& rBootTimer = mBootTimers[eBootTimer];
		rBootTimer.timeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - rBootTimer.startTimePoint);
	}
}

void ProfileManagerBase::BootLog()
{
	if constexpr (kbProfiling)
	{
		BootStop(kBootTimerTotal);

		for (BootTimer& rBootTimer : mBootTimers)
		{
			std::chrono::milliseconds durationMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(rBootTimer.timeNs);
			if (durationMilliseconds.count() > 10)
			{
				Log("{}: {} ms", rBootTimer.name, durationMilliseconds.count());
			}
		}
		Log("\n");
	}
}

void ProfileManagerBase::LogTimers()
{
	if constexpr (kbProfiling)
	{
		Log("");

		{
			std::lock_guard lock(mCpuTimerMutex);

			int64_t iCpuTimerCount = GetCpuTimerCount();
			for (int64_t i = 0; i < iCpuTimerCount; ++i)
			{
				CpuTimer& rCpuTimer = GetCpuTimer(i);
				Log("{}: {} ({}, {}) [{}]", rCpuTimer.name, rCpuTimer.smoothedMicroseconds.Current(), rCpuTimer.smoothedMicroseconds.Average(), rCpuTimer.smoothedMicroseconds.Max(), rCpuTimer.smoothedAllocations.Get());
			}
		}

		Log("");

#if defined(BT_CLIENT)
		int64_t iCommandBufferCount = gpSwapchainManager->mFramebuffers.size();
		for (int64_t i = 0; i < iCommandBufferCount; ++i)
		{
			GpuRead(i, kGpuTimerGlobal, kGpuTimerCount);
		}

		for (int64_t i = 0; i < kGpuTimerCount; ++i)
		{
			GpuTimer& rGpuTimer = mGpuTimers[i];
			Log("{}: {} ({}, {})", rGpuTimer.name, rGpuTimer.smoothedMicroseconds.Current(), rGpuTimer.smoothedMicroseconds.Average(), rGpuTimer.smoothedMicroseconds.Max());
		}

		Log("");
#endif // BT_CLIENT
	}
}

void ProfileManagerBase::UpdateProfileText()
{
	if constexpr (kbProfiling)
	{
#if defined(BT_CLIENT)
		ScopedCpuProfile scopedCpuProfile(kCpuTimerUpdateProfileText);

		int64_t iCpuTimerCount = GetCpuTimerCount();
		{
			std::lock_guard lock(mCpuTimerMutex);
			for (int64_t i = 0; i < iCpuTimerCount; ++i)
			{
				CpuTimer& rCpuTimer = GetCpuTimer(i);
				if (i > kCpuTimerAcquireToGlobal)
				{
					rCpuTimer.smoothedMicroseconds = rCpuTimer.iTotalFrameTimeNs / 1000;
					rCpuTimer.iTotalFrameTimeNs = 0;
					rCpuTimer.smoothedAllocations = rCpuTimer.iAllocationsThisFrame;
					rCpuTimer.iAllocationsThisFrame = 0;
				}
				rCpuTimer.smoothedMicroseconds.Update();
				rCpuTimer.smoothedAllocations.Update();
			}
		}

		mSmoothedAllocations = giAllocationsThisFrame.exchange(0, std::memory_order_relaxed);
		mSmoothedAllocations.Update();

		for (GpuTimer& rGpuTimer : mGpuTimers)
		{
			rGpuTimer.smoothedMicroseconds.Update();
		}

		if (meProfileScreen == ProfileScreen::kOff)
		{
			return;
		}

		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;

		if (meProfileScreen == ProfileScreen::kCpu || meProfileScreen == ProfileScreen::kGpu)
		{
			int64_t iTotalCpuTimeUs = GetCpuTimer(game::kCpuTimerFrameUpdate).smoothedMicroseconds.Get();
			FormatFpsHeader(rWorkbuffer, *this, iTotalCpuTimeUs);
		}

		if (meProfileScreen == ProfileScreen::kCpu)
		{
			FormatCpuScreen(rWorkbuffer, *this);
		}

		if (meProfileScreen == ProfileScreen::kGpu)
		{
			FormatGpuScreen(rWorkbuffer, *this);
		}

		FormatGameScreens(rWorkbuffer);
#endif // BT_CLIENT
	}
}

CpuCounter& ProfileManagerBase::GetCpuCounter(int64_t iIndex)
{
	return mEngineCpuCounters[iIndex];
}

CpuTimer& ProfileManagerBase::GetCpuTimer(int64_t iIndex)
{
	return mEngineCpuTimers[iIndex];
}

int64_t ProfileManagerBase::GetCpuCounterCount() const
{
	return kEngineCpuCounterCount;
}

int64_t ProfileManagerBase::GetCpuTimerCount() const
{
	return kEngineCpuTimerCount;
}

ScopedBootTimer::ScopedBootTimer(BootTimers eBootTimer)
: meBootTimer(eBootTimer)
{
	if constexpr (kbProfiling) { gpProfileManager->BootStart(meBootTimer); }
}

ScopedBootTimer::~ScopedBootTimer()
{
	if constexpr (kbProfiling) { gpProfileManager->BootStop(meBootTimer); }
}

ScopedCpuProfile::ScopedCpuProfile(int64_t iCpuTimer, int64_t iThreads)
: miCpuTimer(iCpuTimer)
{
	if constexpr (kbProfiling) { gpProfileManager->CpuStart(miCpuTimer, iThreads); }
}

ScopedCpuProfile::~ScopedCpuProfile()
{
	if constexpr (kbProfiling) { gpProfileManager->CpuStop(miCpuTimer, false); }
}

} // namespace engine
