#include "ProfileManagerBase.h"

#include "Game.h"
#include "Memory/MemoryManager.h"
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
#ifdef BT_CLIENT
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
#endif
	}
}

void ProfileManagerBase::Destroy()
{
	if constexpr (kbEnableProfiling)
	{
#ifdef BT_CLIENT
		if (gpDeviceManager != nullptr && mVkQueryPool != VK_NULL_HANDLE)
		{
			vkDestroyQueryPool(gpDeviceManager->mVkDevice, mVkQueryPool, nullptr);
		}

		mVkQueryPool = VK_NULL_HANDLE;
#endif
	}
}

void ProfileManagerBase::ToggleProfileText()
{
	if constexpr (kbEnableProfiling)
	{
		meProfileScreen = static_cast<ProfileScreen>((static_cast<uint8_t>(meProfileScreen) + 1) % static_cast<uint8_t>(ProfileScreen::kCount));

#ifdef BT_CLIENT
		for (int64_t i = kTextGraphics; i < kTextAreasCount; ++i)
		{
			gpTextManager->UpdateTextArea(static_cast<TextAreas>(i), "");
		}
#endif
	}
}

void ProfileManagerBase::CpuStart(int64_t iCpuTimer, int64_t iThreads)
{
	if constexpr (kbEnableProfiling)
	{
		std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
		int64_t iAllocations = giAllocationsThisFrame.load(std::memory_order_relaxed);

		std::lock_guard lock(mCpuTimerMutex);

		ScopedSuppressAllocationTracking suppressAllocationTracking;

		std::vector<CpuTimerThreadState>& rThreadStates = mPerThreadTimerStates[std::this_thread::get_id()];
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
	if constexpr (kbEnableProfiling)
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
			std::vector<CpuTimerThreadState>& rThreadStates = mPerThreadTimerStates[std::this_thread::get_id()];
			if (rThreadStates.size() < static_cast<size_t>(GetCpuTimerCount()))
			{
				ScopedSuppressAllocationTracking suppress;
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
	if constexpr (kbEnableProfiling) { GetCpuCounter(iCounter).iCount = iCount; }
}

#ifdef BT_CLIENT
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
		uint32_t uiCount = static_cast<uint32_t>(2 * (kGpuTimerUiRender - kGpuTimerMain));
		vkCmdResetQueryPool(vkCommandBuffer, mVkQueryPool, uiIndex, uiCount);
	}
}

void ProfileManagerBase::ResetUiQueryPool(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer)
{
	if constexpr (kbEnableProfiling)
	{
		if (mVkQueryPool == VK_NULL_HANDLE)
		{
			return;
		}

		uint32_t uiIndex = static_cast<uint32_t>(2 * (kGpuTimerCount * iCommandBuffer + kGpuTimerUiRender));
		uint32_t uiCount = static_cast<uint32_t>(2 * (kGpuTimerCount - kGpuTimerUiRender));
		vkCmdResetQueryPool(vkCommandBuffer, mVkQueryPool, uiIndex, uiCount);
	}
}
#endif

#ifdef BT_CLIENT
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
			mGpuTimers[eGpuTimer].smoothedMicroseconds = static_cast<int64_t>(static_cast<float>(puiResults[1] - puiResults[0]) * gpInstanceManager->mVkPhysicalDeviceProperties.limits.timestampPeriod / 1000.0f);
		}
	}
}
#endif

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

#ifdef BT_CLIENT
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
#endif
	}
}

void ProfileManagerBase::UpdateProfileText()
{
	if constexpr (kbEnableProfiling)
	{
#ifdef BT_CLIENT
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

		// Fps (shared between CPU and GPU screens)
		if (meProfileScreen == ProfileScreen::kCpu || meProfileScreen == ProfileScreen::kGpu)
		{
			rWorkbuffer.Push();
			rWorkbuffer.Append(static_cast<int64_t>(gpGraphics->mRendersInTheLastSecond.Get()));
			rWorkbuffer.Append(" fps");

			int64_t iTotalCpuTimeUs = GetCpuTimer(game::kCpuTimerFrameUpdate).smoothedMicroseconds.Get();
			if (iTotalCpuTimeUs > 100)
			{
				rWorkbuffer.Append(" (Cpu: ");
				rWorkbuffer.Append(1'000'000 / iTotalCpuTimeUs);
				rWorkbuffer.Append(" fps, ");
			}
			else
			{
				rWorkbuffer.Append(" (Cpu: >9000 fps, ");
			}

			int64_t iTotalGpuTime = mGpuTimers[kGpuTimerGlobal].smoothedMicroseconds.Get() + mGpuTimers[kGpuTimerMain].smoothedMicroseconds.Get() + mGpuTimers[kGpuTimerImage].smoothedMicroseconds.Get();
			if (iTotalGpuTime > 0)
			{
				rWorkbuffer.Append("Gpu: ");
				rWorkbuffer.Append(1'000'000 / iTotalGpuTime);
				rWorkbuffer.Append(" fps)");
			}

			rWorkbuffer.Append(" Frame updates: ");
			rWorkbuffer.Append(static_cast<int64_t>(mFullUpdatesInTheLastSecond.Get()));
			rWorkbuffer.Append(" full ");
			rWorkbuffer.Append(static_cast<int64_t>(mInterpolateUpdatesInTheLastSecond.Get()));
			rWorkbuffer.Append(" interpolate");
			gpTextManager->UpdateTextArea(kTextProfileFps, rWorkbuffer.View());
			rWorkbuffer.Pop();
		}

		if (meProfileScreen == ProfileScreen::kCpu)
		{
			// Cpu timers
			rWorkbuffer.Push();
			rWorkbuffer.Append("\n\n");

			for (int64_t i = 0; i < iCpuTimerCount; ++i)
			{
				CpuTimer& rCpuTimer = GetCpuTimer(i);

				int64_t iValue = rCpuTimer.smoothedMicroseconds.Get();
				if (i > kCpuTimerAcquireToGlobal && iValue < 50)
				{
					continue;
				}

				rWorkbuffer.Append(rCpuTimer.name);
				rWorkbuffer.Append(": ");
				rWorkbuffer.Append(iValue);
				rWorkbuffer.Append(" us");
				if (rCpuTimer.iThreads > 1)
				{
					rWorkbuffer.Append(" (");
					rWorkbuffer.Append(rCpuTimer.iThreads);
					rWorkbuffer.Append(")");
				}
				if (rCpuTimer.smoothedAllocations.Get() > 0)
				{
					rWorkbuffer.Append(" [");
					rWorkbuffer.Append(rCpuTimer.smoothedAllocations.Get());
					rWorkbuffer.Append("]");
				}
				rWorkbuffer.Append("\n");

				if (i == kCpuTimerAcquireToGlobal)
				{
					rWorkbuffer.Append("\n");
				}
			}

			gpTextManager->UpdateTextArea(kTextProfileCpuTimers, rWorkbuffer.View());
			rWorkbuffer.Pop();

			// Counters text
			rWorkbuffer.Push();

			int64_t iCpuCounterCount = GetCpuCounterCount();
			for (int64_t i = 0; i < iCpuCounterCount; ++i)
			{
				const CpuCounter& rCpuCounter = GetCpuCounter(i);
				if (rCpuCounter.iCount == 0)
				{
					continue;
				}

				rWorkbuffer.Append(rCpuCounter.name);
				rWorkbuffer.Append(": ");
				rWorkbuffer.Append(rCpuCounter.iCount);
				rWorkbuffer.Append("\n");
			}

			gpTextManager->UpdateTextArea(kTextProfileCpuCounters, rWorkbuffer.View());
			rWorkbuffer.Pop();

			// Memory profiling
			int64_t iEagerBytes = gpFileManager->GetEagerMemoryBytes();
			int64_t iLazyBytes = gpFileManager->GetLazyMemoryBytes();
			int64_t iTotalBytes = iEagerBytes + iLazyBytes;
			int64_t iEagerCount = gpFileManager->GetEagerAllocationCount();
			int64_t iLazyCount = gpFileManager->GetLazyAllocationCount();
			int64_t iTotalCount = iEagerCount + iLazyCount;

			rWorkbuffer.Push();
			rWorkbuffer.Append("Data Memory\n");
			rWorkbuffer.Append("Eager: ");
			rWorkbuffer.AppendFloat(static_cast<float>(iEagerBytes) / (1024.0f * 1024.0f), 1);
			rWorkbuffer.Append(" MB (");
			rWorkbuffer.Append(iEagerCount);
			rWorkbuffer.Append(")\n");
			for (int64_t i = 0; i < data::kDataTypeCount; ++i)
			{
				MemoryStats stats = gpFileManager->GetMemoryStats(static_cast<data::DataTypes>(i));
				if (IsEagerChunk(static_cast<data::DataTypes>(i)))
				{
					rWorkbuffer.Append("  ");
					rWorkbuffer.Append(data::kpcDataTypeNames[i]);
					rWorkbuffer.Append(": ");
					rWorkbuffer.AppendFloat(static_cast<float>(stats.iBytes) / (1024.0f * 1024.0f), 1);
					rWorkbuffer.Append(" MB (");
					rWorkbuffer.Append(stats.iCount);
					rWorkbuffer.Append(")\n");
				}
			}
			rWorkbuffer.Append("Lazy: ");
			rWorkbuffer.AppendFloat(static_cast<float>(iLazyBytes) / (1024.0f * 1024.0f), 1);
			rWorkbuffer.Append(" MB (");
			rWorkbuffer.Append(iLazyCount);
			rWorkbuffer.Append(")\n");
			for (int64_t i = 0; i < data::kDataTypeCount; ++i)
			{
				MemoryStats stats = gpFileManager->GetMemoryStats(static_cast<data::DataTypes>(i));
				if (!IsEagerChunk(static_cast<data::DataTypes>(i)))
				{
					rWorkbuffer.Append("  ");
					rWorkbuffer.Append(data::kpcDataTypeNames[i]);
					rWorkbuffer.Append(": ");
					rWorkbuffer.AppendFloat(static_cast<float>(stats.iBytes) / (1024.0f * 1024.0f), 1);
					rWorkbuffer.Append(" MB (");
					rWorkbuffer.Append(stats.iCount);
					rWorkbuffer.Append(")\n");
				}
			}
			rWorkbuffer.Append("Total: ");
			rWorkbuffer.AppendFloat(static_cast<float>(iTotalBytes) / (1024.0f * 1024.0f), 1);
			rWorkbuffer.Append(" MB (");
			rWorkbuffer.Append(iTotalCount);
			rWorkbuffer.Append(")");
			rWorkbuffer.Append("\nAllocations: ");
			rWorkbuffer.Append(mSmoothedAllocations.Get());
			gpTextManager->UpdateTextArea(kTextProfileMemory, rWorkbuffer.View());
			rWorkbuffer.Pop();
		}

		if (meProfileScreen == ProfileScreen::kGpu)
		{
			// Graphics info
			auto [iX, iY] = FullDetail();
			rWorkbuffer.Push();
			rWorkbuffer.Append(static_cast<int64_t>(gpGraphics->mFramebufferExtent2D.width));
			rWorkbuffer.Append(" x ");
			rWorkbuffer.Append(static_cast<int64_t>(gpGraphics->mFramebufferExtent2D.height));
			rWorkbuffer.Append("\n");
			rWorkbuffer.Append(iX);
			rWorkbuffer.Append(" x ");
			rWorkbuffer.Append(iY);
			rWorkbuffer.Append("\n");
			rWorkbuffer.Append(gpGraphics->miMonitorRefreshRate);
			rWorkbuffer.Append(" Hz\n");
			rWorkbuffer.Append(gMultisampling.Get<bool>() ? "On" : "Off");
			rWorkbuffer.Append(" - ");
			rWorkbuffer.Append(gPresentMode.Get<VkPresentModeKHR>() == VK_PRESENT_MODE_FIFO_KHR ? "Fifo" : (gPresentMode.Get<VkPresentModeKHR>() == VK_PRESENT_MODE_MAILBOX_KHR ? "Mailbox" : "Immediate"));
			gpTextManager->UpdateTextArea(kTextGraphics, rWorkbuffer.View());
			rWorkbuffer.Pop();

			// Gpu timers
			rWorkbuffer.Push();
			rWorkbuffer.Append("\n\n");

			for (GpuTimer& rGpuTimer : mGpuTimers)
			{
				int64_t iValue = rGpuTimer.smoothedMicroseconds.Get();
				int64_t iMax = rGpuTimer.smoothedMicroseconds.Max();
				if (iValue < 10 || (iValue < 200 && !(iMax > 2 * iValue)))
				{
					continue;
				}

				rWorkbuffer.Append(rGpuTimer.name);
				rWorkbuffer.Append(": ");
				rWorkbuffer.Append(iValue);
				rWorkbuffer.Append(" us");
				if (iMax > 2 * iValue)
				{
					rWorkbuffer.Append(" (");
					rWorkbuffer.Append(iMax);
					rWorkbuffer.Append(")");
				}
				rWorkbuffer.Append("\n");
			}

			gpTextManager->UpdateTextArea(kTextProfileGpuTimers, rWorkbuffer.View());
			rWorkbuffer.Pop();
		}

		if (meProfileScreen == ProfileScreen::kFrames)
		{
			rWorkbuffer.Push();
			rWorkbuffer.Append("Frames: ");
			rWorkbuffer.Append(static_cast<int64_t>(game::gpGame->mActiveCoords.size()));
			rWorkbuffer.Append(" [");
			rWorkbuffer.Append(static_cast<int64_t>(game::gpGame->mHumanGridCoord.x));
			rWorkbuffer.Append(",");
			rWorkbuffer.Append(static_cast<int64_t>(game::gpGame->mHumanGridCoord.y));
			rWorkbuffer.Append("]\n");

			int32_t iMinX = game::gpGame->mHumanGridCoord.x;
			int32_t iMaxX = game::gpGame->mHumanGridCoord.x;
			int32_t iMinY = game::gpGame->mHumanGridCoord.y;
			int32_t iMaxY = game::gpGame->mHumanGridCoord.y;
			for (const GridCoord& rCoord : game::gpGame->mActiveCoords)
			{
				iMinX = std::min(iMinX, rCoord.x);
				iMaxX = std::max(iMaxX, rCoord.x);
				iMinY = std::min(iMinY, rCoord.y);
				iMaxY = std::max(iMaxY, rCoord.y);
			}
			for (int32_t y = iMaxY; y >= iMinY; --y)
			{
				for (int32_t x = iMinX; x <= iMaxX; ++x)
				{
					bool bHuman = (x == game::gpGame->mHumanGridCoord.x && y == game::gpGame->mHumanGridCoord.y);
					if (bHuman)
					{
						rWorkbuffer.Append("P");
					}
					else
					{
						bool bActive = false;
						for (const GridCoord& rCoord : game::gpGame->mActiveCoords)
						{
							if (rCoord.x == x && rCoord.y == y)
							{
								bActive = true;
								break;
							}
						}
						rWorkbuffer.Append(bActive ? "#" : "O");
					}
				}
				rWorkbuffer.Append("\n");
			}

			rWorkbuffer.Append("Frame: ");
			rWorkbuffer.Append(game::gpGame->CurrentFrame(game::gpGame->mHumanGridCoord).interpolate.iFrame);
			rWorkbuffer.Append("  Time: ");
			rWorkbuffer.AppendFloat(game::gpGame->CurrentFrame(game::gpGame->mHumanGridCoord).interpolate.fCurrentTime, 1);
			rWorkbuffer.Append("s");
			gpTextManager->UpdateTextArea(kTextProfileFrameStats, rWorkbuffer.View());
			rWorkbuffer.Pop();
		}

		if (meProfileScreen == ProfileScreen::kNetwork)
		{
			rWorkbuffer.Push();
			rWorkbuffer.Append("Network\n");

			if (gpNetworkClient == nullptr)
			{
				rWorkbuffer.Append("(Offline)");
			}
			else
			{
				ENetPeer* pPeer = gpNetworkClient->GetServerPeer();
				if (pPeer != nullptr)
				{
					rWorkbuffer.Append("RTT: ");
					rWorkbuffer.Append(static_cast<int64_t>(pPeer->roundTripTime));
					rWorkbuffer.Append(" ms\nPipeline: ");
					rWorkbuffer.AppendFloat(gpNetworkClient->GetPipelineRttUs() / 1000.0f, 1);
					rWorkbuffer.Append(" ms\nLoss: ");
					rWorkbuffer.AppendFloat(pPeer->packetLoss * 100.0f / 65536.0f, 1);
					rWorkbuffer.Append("%\n");
				}

				rWorkbuffer.Append("In: ");
				int64_t iBytesIn = gpNetworkClient->GetBytesInPerSecond();
				if (iBytesIn >= 1024 * 1024)
				{
					rWorkbuffer.AppendFloat(static_cast<float>(iBytesIn) / (1024.0f * 1024.0f), 1);
					rWorkbuffer.Append(" MB/s");
				}
				else
				{
					rWorkbuffer.AppendFloat(static_cast<float>(iBytesIn) / 1024.0f, 1);
					rWorkbuffer.Append(" KB/s");
				}
				rWorkbuffer.Append("  Out: ");
				int64_t iBytesOut = gpNetworkClient->GetBytesOutPerSecond();
				if (iBytesOut >= 1024 * 1024)
				{
					rWorkbuffer.AppendFloat(static_cast<float>(iBytesOut) / (1024.0f * 1024.0f), 1);
					rWorkbuffer.Append(" MB/s");
				}
				else
				{
					rWorkbuffer.AppendFloat(static_cast<float>(iBytesOut) / 1024.0f, 1);
					rWorkbuffer.Append(" KB/s");
				}
				rWorkbuffer.Append("\nAck: ");
				{
					int64_t iMinAckFloor = -1;
					int64_t iTotalRecv = 0;
					for (const auto& rSlot : gpNetworkClient->GetCoordSlots())
					{
						if (rSlot.eState == CoordSubscriptionState::kActive)
						{
							if (iMinAckFloor < 0 || rSlot.iAckFloor < iMinAckFloor)
							{
								iMinAckFloor = rSlot.iAckFloor;
							}
							iTotalRecv += std::popcount(rSlot.uiReceivedBitfield);
						}
					}
					rWorkbuffer.Append(iMinAckFloor);
					rWorkbuffer.Append("  Confirmed: ");
					rWorkbuffer.Append(game::gpGame->GetConfirmedFrame());
					mSmoothedRecv = iTotalRecv;
				}
				mSmoothedRecv.Update();
				rWorkbuffer.Append("  Recv: ");
				rWorkbuffer.Append(mSmoothedRecv.Get());
				rWorkbuffer.Append("/64");

				mSmoothedRollback = game::gpGame->CurrentFrame(game::gpGame->mHumanGridCoord).interpolate.iFrame - game::gpGame->GetConfirmedFrame();
				mSmoothedRollback.Update();
				mSmoothedBuffer = game::gpGame->GetServerUpdateBufferSize();
				mSmoothedBuffer.Update();
				rWorkbuffer.Append("\nRollback: ");
				rWorkbuffer.Append(mSmoothedRollback.Get());
				rWorkbuffer.Append("  Buffer: ");
				rWorkbuffer.Append(mSmoothedBuffer.Get());
				rWorkbuffer.Append("  Desync: ");
				if (game::gpGame->GetDesyncFrame() >= 0)
				{
					rWorkbuffer.Append("Yes (");
					rWorkbuffer.Append(game::gpGame->GetDesyncFrame());
					rWorkbuffer.Append(")");
				}
				else
				{
					rWorkbuffer.Append("No");
				}
				rWorkbuffer.Append("\nClock: ");
				rWorkbuffer.Append(mSmoothedClockOffset.Get());
				rWorkbuffer.Append("  Target: -");
				rWorkbuffer.Append(mSmoothedClockTarget.Get());
				rWorkbuffer.Append("  Error: ");
				rWorkbuffer.Append(mSmoothedClockError.Get());
			}

			gpTextManager->UpdateTextArea(kTextProfileFps, rWorkbuffer.View());
			rWorkbuffer.Pop();
		}
#endif
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
