#include "ProfileManagerBase.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

ProfileManagerBase::ProfileManagerBase()
{
}

ProfileManagerBase::~ProfileManagerBase()
{
}

void ProfileManagerBase::Create()
{
	if constexpr (kbEnableProfiling)
	{
		if (mVkQueryPool != VK_NULL_HANDLE)
		{
			return;
		}

		// Validate that graphics queue family supports timestamp queries
		uint32_t timestampValidBits = gpInstanceManager->mVkQueueFamilyProperties[gpInstanceManager->miGraphicsQueueFamilyIndex].timestampValidBits;
		if (timestampValidBits == 0)
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
	}
}

void ProfileManagerBase::Destroy()
{
	if constexpr (kbEnableProfiling)
	{
		if (gpDeviceManager != nullptr && mVkQueryPool != VK_NULL_HANDLE)
		{
			vkDestroyQueryPool(gpDeviceManager->mVkDevice, mVkQueryPool, nullptr);
		}

		mVkQueryPool = VK_NULL_HANDLE;
	}
}

void ProfileManagerBase::ToggleProfileText()
{
	if constexpr (kbEnableProfiling)
	{
		mbShowProfileText = !mbShowProfileText;

		if (!mbShowProfileText)
		{
			for (int64_t i = kTextGraphics; i < kTextAreasCount; ++i)
			{
				gpTextManager->UpdateTextArea(static_cast<TextAreas>(i), "");
			}
		}
	}
}

void ProfileManagerBase::CpuStart(int64_t iCpuTimer, int64_t iThreads)
{
	if constexpr (kbEnableProfiling)
	{
		CpuTimer& rCpuTimer = GetCpuTimer(iCpuTimer);
		ASSERT(rCpuTimer.startTimePoint == std::chrono::high_resolution_clock::time_point());
		rCpuTimer.startTimePoint = std::chrono::high_resolution_clock::now();
		rCpuTimer.iThreads = iThreads;
	}
}

void ProfileManagerBase::CpuStop(int64_t iCpuTimer, bool bSmoothNow)
{
	if constexpr (kbEnableProfiling)
	{
		CpuTimer& rCpuTimer = GetCpuTimer(iCpuTimer);
		if (!bSmoothNow) [[likely]]
		{
			ASSERT(rCpuTimer.startTimePoint != std::chrono::high_resolution_clock::time_point());
		}
		rCpuTimer.iTotalFrameTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - rCpuTimer.startTimePoint).count();
		rCpuTimer.startTimePoint = std::chrono::high_resolution_clock::time_point();

		if (bSmoothNow) [[unlikely]]
		{
			rCpuTimer.smoothedMicroseconds = rCpuTimer.iTotalFrameTimeNs / 1000;
			rCpuTimer.iTotalFrameTimeNs = 0;
		}
	}
}

void ProfileManagerBase::SetCount(int64_t iCounter, int64_t iCount)
{
	if constexpr (kbEnableProfiling) { GetCpuCounter(iCounter).iCount = iCount; }
}

void ProfileManagerBase::ResetGlobalQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer)
{
	if constexpr (kbEnableProfiling)
	{
		if (mVkQueryPool == VK_NULL_HANDLE)
		{
			return;
		}

		uint32_t uiIndex = static_cast<uint32_t>(2 * (kGpuTimerCount * iCommandBuffer + kGpuTimerGlobal));
		uint32_t uiCount = static_cast<uint32_t>(2 * (kGpuTimerMain - kGpuTimerGlobal));
		vkCmdResetQueryPool(vkCommandBuffer, mVkQueryPool, uiIndex, uiCount);
	}
}

void ProfileManagerBase::ResetMainQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer)
{
	if constexpr (kbEnableProfiling)
	{
		if (mVkQueryPool == VK_NULL_HANDLE)
		{
			return;
		}

		uint32_t uiIndex = static_cast<uint32_t>(2 * (kGpuTimerCount * iCommandBuffer + kGpuTimerMain));
		uint32_t uiCount = static_cast<uint32_t>(2 * (kGpuTimerCount - kGpuTimerMain));
		vkCmdResetQueryPool(vkCommandBuffer, mVkQueryPool, uiIndex, uiCount);
	}
}

void ProfileManagerBase::GpuStart(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGpuTimer)
{
	if constexpr (kbEnableProfiling)
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
	if constexpr (kbEnableProfiling)
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
	if constexpr (kbEnableProfiling)
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
			mGpuTimers[eGpuTimer].smoothedMicroseconds = static_cast<int64_t>(static_cast<float>(puiResults[1] - puiResults[0]) * gpInstanceManager->mVkPhysicalDeviceProperties.limits.timestampPeriod / 1000.0);
		}
	}
}

void ProfileManagerBase::BootStart(BootTimers eBootTimer)
{
	if constexpr (kbEnableProfiling)
	{
		mBootTimers[eBootTimer].startTimePoint = std::chrono::high_resolution_clock::now();
	}
}

void ProfileManagerBase::BootStop(BootTimers eBootTimer)
{
	if constexpr (kbEnableProfiling)
	{
		BootTimer& rBootTimer = mBootTimers[eBootTimer];
		rBootTimer.timeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - rBootTimer.startTimePoint);
	}
}

void ProfileManagerBase::BootLog()
{
	if constexpr (kbEnableProfiling)
	{
		BootStop(kBootTimerTotal);

		for (BootTimer& rBootTimer : mBootTimers)
		{
			std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(rBootTimer.timeNs);
			if (ms.count() > 10)
			{
				Log("{}: {} ms", rBootTimer.name, ms.count());
			}
		}
		Log("\n");
	}
}

void ProfileManagerBase::LogTimers()
{
	if constexpr (kbEnableProfiling)
	{
		Log("");

		int64_t iCpuTimerCount = GetCpuTimerCount();
		for (int64_t i = 0; i < iCpuTimerCount; ++i)
		{
			CpuTimer& rCpuTimer = GetCpuTimer(i);
			auto us = rCpuTimer.startTimePoint != std::chrono::high_resolution_clock::time_point() ? std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - rCpuTimer.startTimePoint) : 0us;
			if (us == 0us)
			{
				Log("{}: {} ({}, {})", rCpuTimer.name, rCpuTimer.smoothedMicroseconds.Current(), rCpuTimer.smoothedMicroseconds.Average(), rCpuTimer.smoothedMicroseconds.Max());
			}
			else
			{
				Log("{}: {} + {} ({}, {})", rCpuTimer.name, rCpuTimer.smoothedMicroseconds.Current(), us, rCpuTimer.smoothedMicroseconds.Average(), rCpuTimer.smoothedMicroseconds.Max());
			}
		}

		Log("");

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
	}
}

void ProfileManagerBase::UpdateProfileText()
{
	if constexpr (kbEnableProfiling)
	{
		ScopedCpuProfile scopedCpuProfile(kCpuTimerUpdateProfileText);

		int64_t iCpuTimerCount = GetCpuTimerCount();
		for (int64_t i = 0; i < iCpuTimerCount; ++i)
		{
			CpuTimer& rCpuTimer = GetCpuTimer(i);
			if (i > kCpuTimerAcquireToGlobal)
			{
				rCpuTimer.smoothedMicroseconds = rCpuTimer.iTotalFrameTimeNs / 1000;
				rCpuTimer.iTotalFrameTimeNs = 0;
			}

			rCpuTimer.smoothedMicroseconds.Update();
		}

		for (GpuTimer& rGpuTimer : mGpuTimers)
		{
			rGpuTimer.smoothedMicroseconds.Update();
		}

		if (!mbShowProfileText)
		{
			return;
		}

		// Active features
		auto [iX, iY] = FullDetail();
		std::string text;
		text += std::to_string(gpGraphics->mFramebufferExtent2D.width) + " x " + std::to_string(gpGraphics->mFramebufferExtent2D.height) + "\n";
		text += std::to_string(iX) + " x " + std::to_string(iY) + "\n";
		text += std::to_string(gpGraphics->miMonitorRefreshRate) + " Hz\n";
		text += gMultisampling.Get<bool>() ? "On" : "Off";
		text += " - ";
		text += gPresentMode.Get<VkPresentModeKHR>() == VK_PRESENT_MODE_FIFO_KHR ? "Fifo" : (gPresentMode.Get<VkPresentModeKHR>() == VK_PRESENT_MODE_MAILBOX_KHR ? "Mailbox" : "Immediate");

		gpTextManager->UpdateTextArea(kTextGraphics, text);

		// Counters text
		std::string countersText;

		int64_t iCpuCounterCount = GetCpuCounterCount();
		for (int64_t i = 0; i < iCpuCounterCount; ++i)
		{
			const CpuCounter& rCpuCounter = GetCpuCounter(i);
			if (rCpuCounter.iCount == 0)
			{
				continue;
			}

			countersText += rCpuCounter.name;
			countersText += ": ";
			countersText += std::to_string(rCpuCounter.iCount);
			countersText += "\n";
		}

		gpTextManager->UpdateTextArea(kTextProfileCpuCounters, countersText);

		// Cpu timers
		std::string cpuTimersText("\n\n");

		for (int64_t i = 0; i < iCpuTimerCount; ++i)
		{
			CpuTimer& rCpuTimer = GetCpuTimer(i);

			int64_t iValue = rCpuTimer.smoothedMicroseconds.Get();
			if (i > kCpuTimerAcquireToGlobal && iValue < 50)
			{
				continue;
			}

			cpuTimersText += rCpuTimer.name;
			cpuTimersText += ": ";
			cpuTimersText += std::to_string(iValue);
			cpuTimersText += " us";
			if (rCpuTimer.iThreads > 1)
			{
				cpuTimersText += " (" + std::to_string(rCpuTimer.iThreads) + ")";
			}
			cpuTimersText += "\n";

			if (i == kCpuTimerAcquireToGlobal)
			{
				cpuTimersText += "\n";
			}
		}

		gpTextManager->UpdateTextArea(kTextProfileCpuTimers, cpuTimersText);

		// Gpu timers
		std::string gpuTimersText("\n\n");

		for (GpuTimer& rGpuTimer : mGpuTimers)
		{
			int64_t iValue = rGpuTimer.smoothedMicroseconds.Get();
			int64_t iMax = rGpuTimer.smoothedMicroseconds.Max();
			if (iValue < 10 || (iValue < 200 && !(iMax > 2 * iValue)))
			{
				continue;
			}

			gpuTimersText += rGpuTimer.name;
			gpuTimersText += ": ";
			gpuTimersText += std::to_string(iValue);
			gpuTimersText += " us";
			if (iMax > 2 * iValue)
			{
				gpuTimersText += " (";
				gpuTimersText += std::to_string(iMax);
				gpuTimersText += ")";
			}
			gpuTimersText += "\n";
		}

		gpTextManager->UpdateTextArea(kTextProfileGpuTimers, gpuTimersText);

		// Memory profiling
		int64_t iEagerBytes = gpFileManager->GetEagerMemoryBytes();
		int64_t iLazyBytes = gpFileManager->GetLazyMemoryBytes();
		int64_t iTotalBytes = iEagerBytes + iLazyBytes;
		int64_t iEagerCount = gpFileManager->GetEagerAllocationCount();
		int64_t iLazyCount = gpFileManager->GetLazyAllocationCount();
		int64_t iTotalCount = iEagerCount + iLazyCount;

		auto formatMB = [](int64_t iBytes)
		{
			std::ostringstream oss;
			oss << std::fixed << std::setprecision(1) << (static_cast<float>(iBytes) / (1024.0f * 1024.0f));
			return oss.str();
		};

		std::string memoryText;
		memoryText += "Data Memory\n";
		memoryText += "Eager: " + formatMB(iEagerBytes) + " MB (" + std::to_string(iEagerCount) + ")\n";
		for (int64_t i = 0; i < data::kDataTypeCount; ++i)
		{
			MemoryStats stats = gpFileManager->GetMemoryStats(static_cast<data::DataTypes>(i));
			if (IsEagerChunk(static_cast<data::DataTypes>(i)))
			{
				memoryText += "  " + std::string(data::kpcDataTypeNames[i]) + ": " + formatMB(stats.iBytes) + " MB (" + std::to_string(stats.iCount) + ")\n";
			}
		}
		memoryText += "Lazy: " + formatMB(iLazyBytes) + " MB (" + std::to_string(iLazyCount) + ")\n";
		for (int64_t i = 0; i < data::kDataTypeCount; ++i)
		{
			MemoryStats stats = gpFileManager->GetMemoryStats(static_cast<data::DataTypes>(i));
			if (!IsEagerChunk(static_cast<data::DataTypes>(i)))
			{
				memoryText += "  " + std::string(data::kpcDataTypeNames[i]) + ": " + formatMB(stats.iBytes) + " MB (" + std::to_string(stats.iCount) + ")\n";
			}
		}
		memoryText += "Total: " + formatMB(iTotalBytes) + " MB (" + std::to_string(iTotalCount) + ")";

		gpTextManager->UpdateTextArea(kTextProfileMemory, memoryText);

		// Fps
		std::string fpsText(std::to_string(gpGraphics->mRendersInTheLastSecond.Get()));
		fpsText += " fps";

		int64_t iTotalCpuTimeUs = 0;
		iTotalCpuTimeUs = GetCpuTimer(game::kCpuTimerFrameUpdate).smoothedMicroseconds.Get();
		if (iTotalCpuTimeUs > 100)
		{
			fpsText += " (Cpu: ";
			fpsText += std::to_string(1'000'000 / iTotalCpuTimeUs);
			fpsText += " fps, ";
		}
		else
		{
			fpsText += " (Cpu: >9000 fps, ";
		}

		int64_t iTotalGpuTime = mGpuTimers[kGpuTimerGlobal].smoothedMicroseconds.Get() + mGpuTimers[kGpuTimerMain].smoothedMicroseconds.Get() + mGpuTimers[kGpuTimerImage].smoothedMicroseconds.Get();
		if (iTotalGpuTime > 0)
		{
			fpsText += "Gpu: ";
			fpsText += std::to_string(1'000'000 / iTotalGpuTime);
			fpsText += " fps)";
		}

		fpsText += " Frame updates: " + std::to_string(mFullUpdatesInTheLastSecond.Get()) + " full " + std::to_string(mInterpolateUpdatesInTheLastSecond.Get()) + " interpolate";

		gpTextManager->UpdateTextArea(kTextProfileFps, fpsText);
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
	if constexpr (kbEnableProfiling) { gpProfileManager->BootStart(meBootTimer); }
}

ScopedBootTimer::~ScopedBootTimer()
{
	if constexpr (kbEnableProfiling) { gpProfileManager->BootStop(meBootTimer); }
}

ScopedCpuProfile::ScopedCpuProfile(int64_t iCpuTimer, int64_t iThreads)
: miCpuTimer(iCpuTimer)
{
	if constexpr (kbEnableProfiling) { gpProfileManager->CpuStart(miCpuTimer, iThreads); }
}

ScopedCpuProfile::~ScopedCpuProfile()
{
	if constexpr (kbEnableProfiling) { gpProfileManager->CpuStop(miCpuTimer, false); }
}

} // namespace engine
