#include "Pch.h"

#include "ProfileManagerBase.h"

#include "Ui/GraphicsSettingsWrappersBase.h"

#include "Graphics/Camera.h"

namespace engine
{

// Helper: appends timer text into the caller's current workbuffer frame. Caller owns Push/Pop.
void FormatCpuTimersText(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, bool bReevaluate)
{
	// Submit-worker threads write these timer fields under mCpuTimerMutex (CommandBufferManager); lock the identical reads here, matching LogTimers. Cold ~2 Hz path, no contention concern. No caller holds the mutex on the path in (SmoothCpuTimers releases before the formatters run).
	std::lock_guard lock(rProfileManager.mCpuTimerMutex);

	int64_t iCpuTimerCount = rProfileManager.GetCpuTimerCount();

	rWorkbuffer.Append("\n\n");

	for (int64_t i = 0; i < iCpuTimerCount; ++i)
	{
		CpuTimer& rCpuTimer = rProfileManager.GetCpuTimer(i);

		int64_t iValue = rCpuTimer.smoothedMicroseconds.Get();
		if (bReevaluate)
		{
			rCpuTimer.flags.Set(ProfileRowFlags::kVisible, iValue != 0);
		}

		if (!(rCpuTimer.flags & ProfileRowFlags::kVisible))
		{
			continue;
		}

		rWorkbuffer.Append(rProfileManager.GetCpuTimerName(i));
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
}

// Helper: appends counter text into the caller's current workbuffer frame. Caller owns Push/Pop.
void FormatCpuCountersText(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, bool bReevaluate)
{
	int64_t iCpuCounterCount = rProfileManager.GetCpuCounterCount();

	for (int64_t i = 0; i < iCpuCounterCount; ++i)
	{
		CpuCounter& rCpuCounter = rProfileManager.GetCpuCounter(i);
		if (bReevaluate)
		{
			rCpuCounter.flags.Set(ProfileRowFlags::kVisible, rCpuCounter.iCount != 0);
		}

		if (!(rCpuCounter.flags & ProfileRowFlags::kVisible))
		{
			continue;
		}

		rWorkbuffer.Append(rProfileManager.GetCpuCounterName(i));
		rWorkbuffer.Append(": ");
		rWorkbuffer.Append(rCpuCounter.iCount);
		rWorkbuffer.Append("\n");
	}
}

namespace
{

#if defined(BT_CLIENT)
void AppendMemoryStats(common::Workbuffer& rWorkbuffer, bool bEager)
{
	for (int64_t i = 0; i < data::kDataTypeCount; ++i)
	{
		if (IsEagerChunk(static_cast<data::DataTypes>(i)) == bEager)
		{
			MemoryStats stats = gpFileManager->GetMemoryStats(static_cast<data::DataTypes>(i));
			rWorkbuffer.Append("  ");
			rWorkbuffer.Append(data::kpcDataTypeNames[i]);
			rWorkbuffer.Append(": ");
			rWorkbuffer.AppendFloat(static_cast<float>(stats.iBytes) / (1024.0f * 1024.0f), 1);
			rWorkbuffer.Append(" MB (");
			rWorkbuffer.Append(stats.iCount);
			rWorkbuffer.Append(")\n");
		}
	}
}

void FormatGpuGraphicsInfo(common::Workbuffer& rWorkbuffer)
{
	auto [iX, iY] = FullDetail();
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
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
	gpImGuiManager->UpdateTextArea(kTextGraphics, rWorkbuffer.View());
}

void FormatGpuTimerRows(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, bool bReevaluate)
{
	GpuTimer* pGpuTimers = rProfileManager.GetGpuTimers();

	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	rWorkbuffer.Append("\n\n");

	// Active visible-area LOD vertex grid (quads + 1), shared by the water displacement compute pre-pass
	// (writes the top-left rectangle) and the water mesh draw. Mirrors the LOD pick in RenderFrameMain.
	int iWaterLod = std::clamp(game::gpCamera->miVisibleAreaLod, 0, static_cast<int>(BufferManager::kiVisibleAreaLodCount) - 1);
	const BufferManager::VisibleAreaMeshLod& rWaterLod = gpBufferManager->mWaterMeshLods[iWaterLod];
	int64_t iWaterGridX = rWaterLod.iQuadCountX + 1;
	int64_t iWaterGridY = rWaterLod.iQuadCountY + 1;

	for (int64_t i = 0; i < kGpuTimerCount; ++i)
	{
		int64_t iValue = pGpuTimers[i].smoothedMicroseconds.Get();
		int64_t iMax = pGpuTimers[i].smoothedMicroseconds.Max();
		if (bReevaluate)
		{
			pGpuTimers[i].flags.Set(ProfileRowFlags::kVisible, !(iValue < 10 || (iValue < 200 && !(iMax > 2 * iValue))));
		}

		if (!(pGpuTimers[i].flags & ProfileRowFlags::kVisible))
		{
			continue;
		}

		rWorkbuffer.Append(kGpuTimerNames[i]);
		rWorkbuffer.Append(": ");
		rWorkbuffer.Append(iValue);
		rWorkbuffer.Append(" us");
		if (iMax > 2 * iValue)
		{
			rWorkbuffer.Append(" (");
			rWorkbuffer.Append(iMax);
			rWorkbuffer.Append(")");
		}

		// Resolution beside each dynamic-sized pass. Terrain Elevation shows its snap-grid render-target extent;
		// Water Displacement / Water show the active visible-area LOD vertex grid (the top-left rectangle the
		// displacement compute writes / the water mesh draws).
		if (i == kGpuTimerTerrainElevation)
		{
			rWorkbuffer.Append("  ");
			rWorkbuffer.Append(static_cast<int64_t>(gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.mInfo.extent.width));
			rWorkbuffer.Append("x");
			rWorkbuffer.Append(static_cast<int64_t>(gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.mInfo.extent.height));
		}
		else if (i == kGpuTimerWaterDisplacement || i == kGpuTimerWater)
		{
			rWorkbuffer.Append("  ");
			rWorkbuffer.Append(iWaterGridX);
			rWorkbuffer.Append("x");
			rWorkbuffer.Append(iWaterGridY);
		}

		rWorkbuffer.Append("\n");
	}

	gpImGuiManager->UpdateTextArea(kTextProfileGpuTimers, rWorkbuffer.View());
}

void FormatGpuMemoryStats(common::Workbuffer& rWorkbuffer)
{
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	rWorkbuffer.Append("GPU Memory\n");

	VmaTotalStatistics stats {};
	vmaCalculateStatistics(gpDeviceManager->mpAllocator, &stats);

	rWorkbuffer.Append("Allocated: ");
	rWorkbuffer.AppendFloat(static_cast<float>(stats.total.statistics.blockBytes) / (1024.0f * 1024.0f), 1);
	rWorkbuffer.Append(" MB\nUsed: ");
	rWorkbuffer.AppendFloat(static_cast<float>(stats.total.statistics.allocationBytes) / (1024.0f * 1024.0f), 1);
	rWorkbuffer.Append(" MB\nUnused: ");
	rWorkbuffer.AppendFloat(static_cast<float>(stats.total.statistics.blockBytes - stats.total.statistics.allocationBytes) / (1024.0f * 1024.0f), 1);
	rWorkbuffer.Append(" MB\nAllocations: ");
	rWorkbuffer.Append(static_cast<int64_t>(stats.total.statistics.allocationCount));
	rWorkbuffer.Append("  Blocks: ");
	rWorkbuffer.Append(static_cast<int64_t>(stats.total.statistics.blockCount));

	if (gpDeviceManager->mCapabilities & DeviceCapabilityFlags::kMemoryBudgetAvailable)
	{
		uint32_t uiHeapCount = gpInstanceManager->mVkPhysicalDeviceMemoryProperties.memoryHeapCount;
		VmaBudget budgets[VK_MAX_MEMORY_HEAPS] {};
		vmaGetHeapBudgets(gpDeviceManager->mpAllocator, budgets);

		for (uint32_t i = 0; i < uiHeapCount; ++i)
		{
			VkMemoryHeapFlags uiFlags = gpInstanceManager->mVkPhysicalDeviceMemoryProperties.memoryHeaps[i].flags;
			bool bDeviceLocal = (uiFlags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;

			rWorkbuffer.Append("\nHeap ");
			rWorkbuffer.Append(static_cast<int64_t>(i));
			rWorkbuffer.Append(bDeviceLocal ? " (Device Local)\n" : " (Host)\n");

			rWorkbuffer.Append("  Budget: ");
			rWorkbuffer.AppendFloat(static_cast<float>(budgets[i].budget) / (1024.0f * 1024.0f), 1);
			rWorkbuffer.Append(" MB  Usage: ");
			rWorkbuffer.AppendFloat(static_cast<float>(budgets[i].usage) / (1024.0f * 1024.0f), 1);
			rWorkbuffer.Append(" MB");

			if (budgets[i].budget > 0)
			{
				float fPercent = static_cast<float>(static_cast<double>(budgets[i].usage) / static_cast<double>(budgets[i].budget)) * 100.0f;
				rWorkbuffer.Append(" (");
				rWorkbuffer.AppendFloat(fPercent, 1);
				rWorkbuffer.Append("%)");
			}
		}
	}

	gpImGuiManager->UpdateTextArea(kTextProfileMemory, rWorkbuffer.View());
}
#endif // BT_CLIENT

} // namespace

#if defined(BT_CLIENT)

void FormatFpsHeader(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, int64_t iTotalCpuTimeUs)
{
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	rWorkbuffer.Append(static_cast<int64_t>(gpGraphics->mRendersInTheLastSecond.Get()));
	rWorkbuffer.Append(" fps");

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

	GpuTimer* pGpuTimers = rProfileManager.GetGpuTimers();
	int64_t iTotalGpuTime = pGpuTimers[kGpuTimerGlobal].smoothedMicroseconds.Get() + pGpuTimers[kGpuTimerMain].smoothedMicroseconds.Get() + pGpuTimers[kGpuTimerImage].smoothedMicroseconds.Get();
	if (iTotalGpuTime > 0)
	{
		rWorkbuffer.Append("Gpu: ");
		rWorkbuffer.Append(1'000'000 / iTotalGpuTime);
		rWorkbuffer.Append(" fps)");
	}

	rWorkbuffer.Append("   Frame updates: ");
	rWorkbuffer.Append(static_cast<int64_t>(rProfileManager.mFullUpdatesInTheLastSecond.Get()));
	rWorkbuffer.Append(" full ");
	rWorkbuffer.Append(static_cast<int64_t>(rProfileManager.mInterpolateUpdatesInTheLastSecond.Get()));
	rWorkbuffer.Append(" interpolate");
	rWorkbuffer.Append("   Camera height: ");
	rWorkbuffer.Append(static_cast<int64_t>(game::gpCamera->mfCameraEyeHeight));
	rWorkbuffer.Append(" m");
	gpImGuiManager->UpdateTextArea(kTextProfileFps, rWorkbuffer.View());
}

void FormatCpuScreen(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, bool bReevaluate)
{
	// Cpu timers
	{
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
		FormatCpuTimersText(rWorkbuffer, rProfileManager, bReevaluate);
		gpImGuiManager->UpdateTextArea(kTextProfileCpuTimers, rWorkbuffer.View());
	}

	// Counters text
	{
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
		FormatCpuCountersText(rWorkbuffer, rProfileManager, bReevaluate);
		gpImGuiManager->UpdateTextArea(kTextProfileCpuCounters, rWorkbuffer.View());
	}

	// Memory profiling
	MemoryStats eagerStats = gpFileManager->GetEagerStats();
	MemoryStats lazyStats = gpFileManager->GetLazyStats();
	int64_t iTotalBytes = eagerStats.iBytes + lazyStats.iBytes;
	int64_t iTotalCount = eagerStats.iCount + lazyStats.iCount;

	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	rWorkbuffer.Append("Data Memory\n");
	rWorkbuffer.Append("Eager: ");
	rWorkbuffer.AppendFloat(static_cast<float>(eagerStats.iBytes) / (1024.0f * 1024.0f), 1);
	rWorkbuffer.Append(" MB (");
	rWorkbuffer.Append(eagerStats.iCount);
	rWorkbuffer.Append(")\n");
	AppendMemoryStats(rWorkbuffer, true);
	rWorkbuffer.Append("Lazy: ");
	rWorkbuffer.AppendFloat(static_cast<float>(lazyStats.iBytes) / (1024.0f * 1024.0f), 1);
	rWorkbuffer.Append(" MB (");
	rWorkbuffer.Append(lazyStats.iCount);
	rWorkbuffer.Append(")\n");
	AppendMemoryStats(rWorkbuffer, false);
	rWorkbuffer.Append("Total: ");
	rWorkbuffer.AppendFloat(static_cast<float>(iTotalBytes) / (1024.0f * 1024.0f), 1);
	rWorkbuffer.Append(" MB (");
	rWorkbuffer.Append(iTotalCount);
	rWorkbuffer.Append(")");
	rWorkbuffer.Append("\nAllocations: ");
	rWorkbuffer.Append(rProfileManager.GetSmoothedAllocations().Get());
	gpImGuiManager->UpdateTextArea(kTextProfileMemory, rWorkbuffer.View());
}

void FormatGpuScreen(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, bool bReevaluate)
{
	FormatGpuGraphicsInfo(rWorkbuffer);
	FormatGpuTimerRows(rWorkbuffer, rProfileManager, bReevaluate);
	FormatGpuMemoryStats(rWorkbuffer);
}

#endif // BT_CLIENT

} // namespace engine
