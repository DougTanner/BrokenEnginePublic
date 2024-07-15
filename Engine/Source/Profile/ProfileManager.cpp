#include "ProfileManager.h"

#include "Graphics/Graphics.h"

namespace engine
{

#if defined(ENABLE_PROFILING)

ProfileManager::ProfileManager()
{
	gpProfileManager = this;

	BootStart(kBootTimerTotal);
}

ProfileManager::~ProfileManager()
{
	gpProfileManager = nullptr;
}

void ProfileManager::Create()
{
	if (mVkQueryPool != VK_NULL_HANDLE)
	{
		return;
	}

	int64_t iQueryCount = gpCommandBufferManager->CommandBufferCount() * 2 * kGpuTimerCount;

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
}

void ProfileManager::Destroy()
{
	if (gpDeviceManager != nullptr && mVkQueryPool != VK_NULL_HANDLE)
	{
		vkDestroyQueryPool(gpDeviceManager->mVkDevice, mVkQueryPool, nullptr);
	}

	mVkQueryPool = VK_NULL_HANDLE;
}

void ProfileManager::ToggleProfileText()
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

void ProfileManager::CpuStart(CpuTimers eCpuTimer, int64_t iThreads)
{
	ASSERT(gpCpuTimers[eCpuTimer].startTimePoint == std::chrono::high_resolution_clock::time_point());
	gpCpuTimers[eCpuTimer].startTimePoint = std::chrono::high_resolution_clock::now();
	gpCpuTimers[eCpuTimer].iThreads = iThreads;
}

void ProfileManager::CpuStop(CpuTimers eCpuTimer, bool bSmoothNow)
{
	CpuTimer& rCpuTimer = gpCpuTimers[eCpuTimer];
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

void ProfileManager::ResetGlobalQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer)
{
	uint32_t uiIndex = static_cast<uint32_t>(2 * (kGpuTimerCount * iCommandBuffer + kGpuTimerGlobal));
	uint32_t uiCount = static_cast<uint32_t>(2 * (kGpuTimerMain - kGpuTimerGlobal));
	vkCmdResetQueryPool(vkCommandBuffer, mVkQueryPool, uiIndex, uiCount);
}

void ProfileManager::ResetMainQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer)
{
	uint32_t uiIndex = static_cast<uint32_t>(2 * (kGpuTimerCount * iCommandBuffer + kGpuTimerMain));
	uint32_t uiCount = static_cast<uint32_t>(2 * (kGpuTimerImage - kGpuTimerMain));
	vkCmdResetQueryPool(vkCommandBuffer, mVkQueryPool, uiIndex, uiCount);
}

void ProfileManager::ResetImageQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer)
{
	uint32_t uiIndex = static_cast<uint32_t>(2 * (kGpuTimerCount * iCommandBuffer + kGpuTimerImage));
	uint32_t uiCount = static_cast<uint32_t>(2 * (kGpuTimerCount - kGpuTimerImage));
	vkCmdResetQueryPool(vkCommandBuffer, mVkQueryPool, uiIndex, uiCount);
}

void ProfileManager::GpuStart(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGpuTimer)
{
	uint32_t uiCounterIndex = static_cast<uint32_t>(2 * kGpuTimerCount * iCommandBuffer + 2 * eGpuTimer);
	vkCmdWriteTimestamp(vkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mVkQueryPool, uiCounterIndex);

	bool bRoot = eGpuTimer == kGpuTimerGlobal || eGpuTimer == kGpuTimerMain || eGpuTimer == kGpuTimerImage;
	auto vkCmdBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(gpInstanceManager->mVkInstance, "vkCmdBeginDebugUtilsLabelEXT"));
	if (vkCmdBeginDebugUtilsLabelEXT)
	{
		float fColor = bRoot ? 0.5f : 0.0f;
		VkDebugUtilsLabelEXT vkDebugUtilsLabelEXT =
		{
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
			.pLabelName = gpGpuTimers[eGpuTimer].pcName.data(),
			.color = {fColor, fColor, fColor, fColor},
		};
		vkCmdBeginDebugUtilsLabelEXT(vkCommandBuffer, &vkDebugUtilsLabelEXT);
	}
}

void ProfileManager::GpuStop(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGpuTimer)
{
	uint32_t uiCounterIndex = static_cast<uint32_t>(2 * kGpuTimerCount * iCommandBuffer + 2 * eGpuTimer + 1);
	vkCmdWriteTimestamp(vkCommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mVkQueryPool, uiCounterIndex);

	auto vkCmdEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(gpInstanceManager->mVkInstance, "vkCmdEndDebugUtilsLabelEXT"));
	if (vkCmdEndDebugUtilsLabelEXT)
	{
		vkCmdEndDebugUtilsLabelEXT(vkCommandBuffer);
	}
}

void ProfileManager::GpuRead(int64_t iCommandBuffer, GpuTimers eStart, GpuTimers eEnd)
{
	for (int64_t i = eStart; i < eEnd; ++i)
	{
		GpuTimers eGpuTimer = static_cast<GpuTimers>(i);
		uint64_t puiResults[2] {};
		uint32_t uiCounterIndex = static_cast<uint32_t>(2 * kGpuTimerCount * iCommandBuffer + 2 * eGpuTimer);
		VkResult vkResultGetQueryPoolResults = vkGetQueryPoolResults(gpDeviceManager->mVkDevice, mVkQueryPool, uiCounterIndex, 2, sizeof(puiResults), puiResults, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
		if (vkResultGetQueryPoolResults == VK_NOT_READY) [[unlikely]]
		{
			// Should not happen if we check that command buffer was executed, and wait on fence
			continue;
		}
		CHECK_VK(vkResultGetQueryPoolResults);

		GpuTimer& rGpuTimer = gpGpuTimers[eGpuTimer];
		rGpuTimer.smoothedMicroseconds = static_cast<int64_t>((puiResults[1] - puiResults[0]) / 1000);
	}
}

void ProfileManager::BootStart(BootTimers eBootTimer)
{
	gpBootTimers[eBootTimer].startTimePoint = std::chrono::high_resolution_clock::now();
}

void ProfileManager::BootStop(BootTimers eBootTimer)
{
	BootTimer& rBootTimer = gpBootTimers[eBootTimer];
	rBootTimer.timeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - rBootTimer.startTimePoint);
}

void ProfileManager::BootLog()
{
	BootStop(kBootTimerTotal);

	for (BootTimer& rBootTimer : gpBootTimers)
	{
		std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(rBootTimer.timeNs);
		if (ms.count() > 10)
		{
			common::Log("{}: {} ms", rBootTimer.name, ms.count());
		}
	}
	common::Log("\n");
}

void ProfileManager::LogTimers()
{
	LOG("");

	for (int64_t i = 0; i < kCpuTimerCount; ++i)
	{
		CpuTimer& rCpuTimer = gpCpuTimers[i];
		auto us = rCpuTimer.startTimePoint != std::chrono::high_resolution_clock::time_point() ? std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - rCpuTimer.startTimePoint) : 0us;
		if (us == 0us)
		{
			LOG("{}: {} ({}, {})", gpCpuTimers[i].pcName, rCpuTimer.smoothedMicroseconds.Current(), rCpuTimer.smoothedMicroseconds.Average(), rCpuTimer.smoothedMicroseconds.Max());
		}
		else
		{
			LOG("{}: {} + {} ({}, {})", gpCpuTimers[i].pcName, rCpuTimer.smoothedMicroseconds.Current(), us, rCpuTimer.smoothedMicroseconds.Average(), rCpuTimer.smoothedMicroseconds.Max());
		}
	}

	LOG("");

	// Reading GPU timers for all command buffers, wait for GPU to be idle
	vkDeviceWaitIdle(gpDeviceManager->mVkDevice);

	int64_t iCommandBufferCount = gpCommandBufferManager->CommandBufferCount();
	for (int64_t i = 0; i < iCommandBufferCount; ++i)
	{
		GPU_PROFILE_READ(i, kGpuTimerGlobal, kGpuTimerCount);
	}

	for (int64_t i = 0; i < kGpuTimerCount; ++i)
	{
		GpuTimer& rGpuTimer = gpGpuTimers[i];
		LOG("{}: {} ({}, {})", gpGpuTimers[i].pcName, rGpuTimer.smoothedMicroseconds.Current(), rGpuTimer.smoothedMicroseconds.Average(), rGpuTimer.smoothedMicroseconds.Max());
	}

	LOG("");
}

void ProfileManager::UpdateProfileText()
{
	SCOPED_CPU_PROFILE(kCpuTimerUpdateProfileText);

	for (int64_t i = 0; i < kCpuTimerCount; ++i)
	{
		CpuTimer& rCpuTimer = gpCpuTimers[i];
		if (i > kCpuTimerAcquireToGlobal)
		{
			rCpuTimer.smoothedMicroseconds = rCpuTimer.iTotalFrameTimeNs / 1000;
			rCpuTimer.iTotalFrameTimeNs = 0;
		}

		rCpuTimer.smoothedMicroseconds.Update();
	}

	for (GpuTimer& rGpuTimer : gpGpuTimers)
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

	for (const CpuCounter& rCpuCounter : gpCpuCounters)
	{
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

	for (int64_t i = 0; i < kCpuTimerCount; ++i)
	{
		CpuTimer& rCpuTimer = gpCpuTimers[i];

		int64_t iValue = rCpuTimer.smoothedMicroseconds.Get();
		if (i > kCpuTimerAcquireToGlobal && iValue < 50)
		{
			continue;
		}

		cpuTimersText += rCpuTimer.pcName;
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

	for (GpuTimer& rGpuTimer : gpGpuTimers)
	{
		int64_t iValue = rGpuTimer.smoothedMicroseconds.Get();
		int64_t iMax = rGpuTimer.smoothedMicroseconds.Max();
		if (iValue < 10 || (iValue < 200 && !(iMax > 2 * iValue)))
		{
			continue;
		}

		gpuTimersText += rGpuTimer.pcName;
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

	// Fps
	std::string fpsText(std::to_string(gpGraphics->mRendersInTheLastSecond.Get()));
	fpsText += " fps";

	int64_t iTotalCpuTimeUs = 0;
	iTotalCpuTimeUs = gpCpuTimers[kCpuTimerFrameUpdate].smoothedMicroseconds.Get();
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

	int64_t iTotalGpuTime = gpGpuTimers[kGpuTimerGlobal].smoothedMicroseconds.Get() + gpGpuTimers[kGpuTimerMain].smoothedMicroseconds.Get() + gpGpuTimers[kGpuTimerImage].smoothedMicroseconds.Get();
	if (iTotalGpuTime > 0)
	{
		fpsText += "Gpu: ";
		fpsText += std::to_string(1'000'000 / iTotalGpuTime);
		fpsText += " fps)";
	}

	fpsText += " updates: " + std::to_string(mUpdatesInTheLastSecond.Get());

	gpTextManager->UpdateTextArea(kTextProfileFps, fpsText);
}

#endif // ENABLE_PROFILING

} // namespace engine
