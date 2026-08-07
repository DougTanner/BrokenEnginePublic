#include "Pch.h"

#include "ProfileManagerBase.h"

#include "Memory/GlobalAllocator.h"
#include "Profile/ProfileManager.h"

#include "Game.h"

namespace engine
{

// The engine consumes game CPU timers/counters by name (GetCpuTimer(game::kCpuTimerFrameUpdate) below; GameBase phase brackets), and the base GetCpuTimer/GetCpuCounter accessors dispatch on a contiguous index space where the first game enumerator must start at the engine count. Pin that contract here so an omitted game-enum initializer is a compile error, not a silent misroute of every game index into the engine arrays.
static_assert(static_cast<int64_t>(game::kCpuTimerFrameUpdate) == static_cast<int64_t>(kEngineCpuTimerCount), "First game CPU timer must start at kEngineCpuTimerCount (contiguous engine->game index space).");
static_assert(static_cast<int64_t>(game::kCpuCounterPlayers) == static_cast<int64_t>(kEngineCpuCounterCount), "First game CPU counter must start at kEngineCpuCounterCount (contiguous engine->game index space).");

// All profile-text rows re-evaluate their show/hide state together on this cadence; a state therefore persists at least this long.
constexpr std::chrono::seconds kProfileVisibilityInterval = 2s;

#if defined(BT_CLIENT)
// kbProfilingDump mode: one CSV sample of every timer/counter per interval, written for offline analysis.
constexpr std::chrono::seconds kProfileDumpInterval = 1s;
#endif // BT_CLIENT

ProfileManagerBase::ProfileManagerBase(CpuCounter* pGameCpuCounters, CpuTimer* pGameCpuTimers, const std::string_view* pGameCpuCounterNames, const std::string_view* pGameCpuTimerNames, int64_t iCpuCounterCount, int64_t iCpuTimerCount)
: mpGameCpuCounters(pGameCpuCounters)
, mpGameCpuTimers(pGameCpuTimers)
, mpGameCpuCounterNames(pGameCpuCounterNames)
, mpGameCpuTimerNames(pGameCpuTimerNames)
, miCpuCounterCount(iCpuCounterCount)
, miCpuTimerCount(iCpuTimerCount)
{
#if defined(BT_SERVER)
	if constexpr (kbProfiling)
	{
		mpRawCpuTimers = std::make_unique<RawCpuTimerState[]>(static_cast<size_t>(iCpuTimerCount));
	}
#endif // BT_SERVER
}

void ProfileManagerBase::Create()
{
	if constexpr (kbProfiling)
	{
#if defined(BT_CLIENT)
		// Precondition: managers are created in strict order Instance -> Device -> Swapchain, then this runs (Graphics::Create calls it after swapchain creation), so gpInstanceManager/gpDeviceManager/gpSwapchainManager and OneShotCommandBuffer are all live below. Server body is empty (managers client-only).
		if constexpr (kbProfilingDump)
		{
			if (mpDumpLog == nullptr)
			{
				// Runs once, on the boot-time Create() (allocation tracking not yet armed); later Create() calls skip via the null guard above. DiagnosticLog's ctor creates the parent directory; its Write is stack-buffered, flushed per line, and suppresses allocation tracking, so the per-second dump is main-loop safe.
				wchar_t pcDirectory[MAX_PATH] {};
				uint32_t uiTempPathLength = GetTempPathW(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
				if (uiTempPathLength != 0 && uiTempPathLength < std::size(pcDirectory) - 1)
				{
					std::filesystem::path filename(pcDirectory);
					filename /= game::kGameName;
					filename /= "ProfileDump.csv";
					mpDumpLog = std::make_unique<common::DiagnosticLog>(3, filename.string().c_str());
					mpDumpLog->Write("ms,type,index,name,current,average,max,allocations");

					mDumpStartTime = std::chrono::steady_clock::now();
					mLastDumpTime = mDumpStartTime;
				}
			}
		}

		if (mVkQueryPool != VK_NULL_HANDLE)
		{
			return;
		}

		// Validate that graphics queue family supports timestamp queries
		uint32_t uiTimestampValidBits = gpInstanceManager->mVkQueueFamilyProperties[gpInstanceManager->miGraphicsQueueFamilyIndex].timestampValidBits;
		if (uiTimestampValidBits == 0)
		{
			LOG(kDefault, kWarning, "Warning: Graphics queue family does not support timestamp queries. GPU profiling disabled.");
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
		oneShotCommandBuffer.Execute();
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
		mGpuShadowSample = {};

		// mpDumpLog deliberately not reset: Destroy() also runs on swapchain-tier recreates (resize, settings), and the dump file must span the whole session. The unique_ptr closes it at ProfileManager destruction.
#endif // BT_CLIENT
	}
}

bool ProfileManagerBase::TickVisibilityCadence()
{
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	bool bReevaluate = now - mLastVisibilityEvalTime >= kProfileVisibilityInterval;
	if (bReevaluate)
	{
		mLastVisibilityEvalTime = now;
	}

	return bReevaluate;
}

void ProfileManagerBase::ToggleProfileText()
{
	if constexpr (kbProfiling)
	{
		meProfileScreen = static_cast<ProfileScreen>((static_cast<uint8_t>(meProfileScreen) + 1) % static_cast<uint8_t>(ProfileScreen::kCount));

		// Re-evaluate visibility immediately on the switched-to screen instead of showing stale flags.
		mLastVisibilityEvalTime = {};

#if defined(BT_CLIENT)
		for (int64_t i = kTextGraphics; i < kTextAreasCount; ++i)
		{
			gpImGuiManager->UpdateTextArea(static_cast<TextAreas>(i), "");
		}
#endif
	}
}

void ProfileManagerBase::CpuStart(int64_t iCpuTimer, int64_t iThreads)
{
	if constexpr (kbProfiling)
	{
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		int64_t iAllocations = giAllocationsThisFrame.load(std::memory_order_relaxed);

		std::lock_guard lock(mCpuTimerMutex);

		// Heap: try_emplace into mPerThreadTimerStates + vector::resize to kCpuTimerCount
		ScopedSuppressAllocationTracking suppress;

		std::vector<CpuTimerThreadState>& rThreadStates = mPerThreadTimerStates.try_emplace(std::this_thread::get_id()).first->second;
		if (rThreadStates.size() < static_cast<size_t>(GetCpuTimerCount()))
		{
			rThreadStates.resize(static_cast<size_t>(GetCpuTimerCount()));
		}

		CpuTimerThreadState& rState = rThreadStates[static_cast<size_t>(iCpuTimer)];
		ASSERT(rState.startTimePoint == std::chrono::steady_clock::time_point());
		rState.startTimePoint = now;
		rState.iStartAllocations = iAllocations;

		GetCpuTimer(iCpuTimer).iThreads = iThreads;
	}
}

void ProfileManagerBase::CpuStop(int64_t iCpuTimer, CpuStopFlags_t flags)
{
	if constexpr (kbProfiling)
	{
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		int64_t iAllocations = giAllocationsThisFrame.load(std::memory_order_relaxed);

		std::lock_guard lock(mCpuTimerMutex);

		CpuTimerThreadState* pState = nullptr;

		if (flags & CpuStopFlags::kCrossThread)
		{
			// Search all threads for the one that started this timer
			for (auto& [rThreadId, rStates] : mPerThreadTimerStates)
			{
				if (rStates.size() > static_cast<size_t>(iCpuTimer) && rStates[static_cast<size_t>(iCpuTimer)].startTimePoint != std::chrono::steady_clock::time_point())
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
				// Heap: vector::resize to kCpuTimerCount
				ScopedSuppressAllocationTracking suppress;
				rThreadStates.resize(static_cast<size_t>(GetCpuTimerCount()));
			}
			pState = &rThreadStates[static_cast<size_t>(iCpuTimer)];
			ASSERT(pState->startTimePoint != std::chrono::steady_clock::time_point());
		}

		CpuTimer& rCpuTimer = GetCpuTimer(iCpuTimer);

		if (pState != nullptr) [[likely]]
		{
			int64_t iElapsedNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(now - pState->startTimePoint).count();
			rCpuTimer.iTotalFrameTimeNs += iElapsedNanoseconds;
			pState->startTimePoint = std::chrono::steady_clock::time_point();
			rCpuTimer.iAllocationsThisFrame += std::max(static_cast<int64_t>(0), iAllocations - pState->iStartAllocations);

#if defined(BT_SERVER)
			if constexpr (kbProfiling)
			{
				RawCpuTimerState& rRawTimer = mpRawCpuTimers[static_cast<size_t>(iCpuTimer)];
				if (rRawTimer.flags & RawCpuTimerStateFlags::kRegistered)
				{
					rRawTimer.iTotalTimeNs += iElapsedNanoseconds;
					++rRawTimer.iInvocationCount;
				}
			}
#endif // BT_SERVER
		}

		if (flags & CpuStopFlags::kSmoothNow) [[unlikely]]
		{
			rCpuTimer.flags.Set(ProfileRowFlags::kSmoothAtStop);
			rCpuTimer.smoothedMicroseconds = rCpuTimer.iTotalFrameTimeNs / 1000;
			rCpuTimer.iTotalFrameTimeNs = 0;
			rCpuTimer.smoothedAllocations = rCpuTimer.iAllocationsThisFrame;
			rCpuTimer.iAllocationsThisFrame = 0;
		}
	}
}

#if defined(BT_SERVER)
void ProfileManagerBase::RegisterRawCpuTimer(int64_t iCpuTimer)
{
	if constexpr (kbProfiling)
	{
		ASSERT(mpRawCpuTimers != nullptr);
		mpRawCpuTimers[static_cast<size_t>(iCpuTimer)].flags.Set(RawCpuTimerStateFlags::kRegistered);
	}
}

void ProfileManagerBase::RegisterRawCpuTimerEvent(int64_t iCpuTimer)
{
	if constexpr (kbProfiling)
	{
		ASSERT(mpRawCpuTimers != nullptr);
		mpRawCpuTimers[static_cast<size_t>(iCpuTimer)].flags.Set(RawCpuTimerStateFlags::kEventRegistered);
	}
}

void ProfileManagerBase::AddRawCpuTimerAuxiliaryCount(int64_t iCpuTimer, int64_t iCount)
{
	if constexpr (kbProfiling)
	{
		mpRawCpuTimers[static_cast<size_t>(iCpuTimer)].iAuxiliaryCount.fetch_add(iCount, std::memory_order_relaxed);
	}
}

void ProfileManagerBase::LatchRawCpuTimer(int64_t iCpuTimer, bool bAccept)
{
	if constexpr (kbProfiling)
	{
		std::lock_guard lock(mCpuTimerMutex);
		RawCpuTimerState& rRawTimer = mpRawCpuTimers[static_cast<size_t>(iCpuTimer)];
		int64_t iAuxiliaryCount = rRawTimer.iAuxiliaryCount.exchange(0, std::memory_order_relaxed);
		if (bAccept)
		{
			++rRawTimer.record.uiSampleSequence;
			rRawTimer.record.iSampleUs = rRawTimer.iTotalTimeNs / 1000;
			rRawTimer.record.iInvocationCount = rRawTimer.iInvocationCount;
			rRawTimer.record.iAuxiliaryCount = iAuxiliaryCount;
		}
		rRawTimer.iTotalTimeNs = 0;
		rRawTimer.iInvocationCount = 0;
		if (!bAccept)
		{
			rRawTimer.flags.Clear(RawCpuTimerStateFlags::kEventArmed);
			rRawTimer.iMinimumSampleTick = 0;
		}
	}
}

void ProfileManagerBase::LatchRawCpuTimers(bool bAccept, int64_t iSampleTick)
{
	if constexpr (kbProfiling)
	{
		std::lock_guard lock(mCpuTimerMutex);
		for (int64_t i = 0; i < miCpuTimerCount; ++i)
		{
			RawCpuTimerState& rRawTimer = mpRawCpuTimers[static_cast<size_t>(i)];
			if (!(rRawTimer.flags & RawCpuTimerStateFlags::kRegistered))
			{
				continue;
			}

			int64_t iAuxiliaryCount = rRawTimer.iAuxiliaryCount.exchange(0, std::memory_order_relaxed);
			if (bAccept)
			{
				++rRawTimer.record.uiSampleSequence;
				rRawTimer.record.iSampleUs = rRawTimer.iTotalTimeNs / 1000;
				rRawTimer.record.iInvocationCount = rRawTimer.iInvocationCount;
				rRawTimer.record.iAuxiliaryCount = iAuxiliaryCount;
			}
			rRawTimer.iTotalTimeNs = 0;
			rRawTimer.iInvocationCount = 0;
		}

		if (!bAccept)
		{
			// A rejected latch invalidates only unpublished arms. Retained payloads and their metadata remain diagnostic state.
			for (int64_t i = 0; i < miCpuTimerCount; ++i)
			{
				RawCpuTimerState& rRawTimer = mpRawCpuTimers[static_cast<size_t>(i)];
				rRawTimer.flags.Clear(RawCpuTimerStateFlags::kEventArmed);
				rRawTimer.iMinimumSampleTick = 0;
			}
		}
		else
		{
			OnRawCpuTimersLatched(iSampleTick);
		}
	}
}

RawCpuTimerRecord ProfileManagerBase::GetRawCpuTimer(int64_t iCpuTimer) const
{
	if constexpr (kbProfiling)
	{
		return mpRawCpuTimers[static_cast<size_t>(iCpuTimer)].record;
	}
	else
	{
		return {};
	}
}

bool ProfileManagerBase::ArmRawCpuTimerEvent(int64_t iCpuTimer, int64_t iMinimumSampleTick)
{
	if constexpr (kbProfiling)
	{
		std::lock_guard lock(mCpuTimerMutex);
		return ArmRawCpuTimerEventLocked(iCpuTimer, iMinimumSampleTick);
	}
	else
	{
		return false;
	}
}

bool ProfileManagerBase::ArmRawCpuTimerEventLocked(int64_t iCpuTimer, int64_t iMinimumSampleTick)
{
	if constexpr (kbProfiling)
	{
		RawCpuTimerState& rRawTimer = mpRawCpuTimers[static_cast<size_t>(iCpuTimer)];
		if (!(rRawTimer.flags & RawCpuTimerStateFlags::kEventRegistered) || rRawTimer.eventRecord.flags & RawCpuTimerEventFlags::kAvailable || rRawTimer.eventRecord.flags & RawCpuTimerEventFlags::kOverrun)
		{
			return false;
		}

		rRawTimer.flags.Set(RawCpuTimerStateFlags::kEventArmed);
		rRawTimer.iMinimumSampleTick = iMinimumSampleTick;
		return true;
	}
	else
	{
		return false;
	}
}

bool ProfileManagerBase::PublishRawCpuTimerEvent(int64_t iCpuTimer, int64_t iSampleTick)
{
	if constexpr (kbProfiling)
	{
		RawCpuTimerState& rRawTimer = mpRawCpuTimers[static_cast<size_t>(iCpuTimer)];
		RawCpuTimerEventRecord& rEvent = rRawTimer.eventRecord;
		if (rEvent.flags & RawCpuTimerEventFlags::kAvailable)
		{
			rEvent.flags.Set(RawCpuTimerEventFlags::kOverrun);
			return false;
		}
		if (!(rRawTimer.flags & RawCpuTimerStateFlags::kEventRegistered) || !(rRawTimer.flags & RawCpuTimerStateFlags::kEventArmed) || rEvent.flags & RawCpuTimerEventFlags::kOverrun || iSampleTick < rRawTimer.iMinimumSampleTick)
		{
			return false;
		}

		++rEvent.uiEventSequence;
		rEvent.uiSampleSequence = rRawTimer.record.uiSampleSequence;
		rEvent.iSampleTick = iSampleTick;
		rEvent.iSampleUs = rRawTimer.record.iSampleUs;
		rEvent.iInvocationCount = rRawTimer.record.iInvocationCount;
		rEvent.iAuxiliaryCount = rRawTimer.record.iAuxiliaryCount;
		rEvent.flags.Set(RawCpuTimerEventFlags::kAvailable);
		rEvent.flags.Clear(RawCpuTimerEventFlags::kOverrun);
		rRawTimer.flags.Clear(RawCpuTimerStateFlags::kEventArmed);
		rRawTimer.iMinimumSampleTick = 0;
		return true;
	}
	else
	{
		return false;
	}
}

RawCpuTimerEventRecord ProfileManagerBase::GetRawCpuTimerEvent(int64_t iCpuTimer) const
{
	if constexpr (kbProfiling)
	{
		return mpRawCpuTimers[static_cast<size_t>(iCpuTimer)].eventRecord;
	}
	else
	{
		return {};
	}
}

bool ProfileManagerBase::AcknowledgeRawCpuTimerEvent(int64_t iCpuTimer, uint64_t uiEventSequence)
{
	if constexpr (kbProfiling)
	{
		RawCpuTimerEventRecord& rEvent = mpRawCpuTimers[static_cast<size_t>(iCpuTimer)].eventRecord;
		if (!(rEvent.flags & RawCpuTimerEventFlags::kAvailable) || uiEventSequence == 0 || uiEventSequence != rEvent.uiEventSequence)
		{
			return false;
		}

		rEvent.flags.Clear({RawCpuTimerEventFlags::kAvailable, RawCpuTimerEventFlags::kOverrun});
		return true;
	}
	else
	{
		return false;
	}
}
#endif // BT_SERVER

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
		vkCmdWriteTimestamp(vkCommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mVkQueryPool, uiCounterIndex);

		bool bRoot = eGpuTimer == kGpuTimerGlobal || eGpuTimer == kGpuTimerMain || eGpuTimer == kGpuTimerImage;
		if (vkCmdBeginDebugUtilsLabelEXT != nullptr)
		{
			float fColor = bRoot ? 0.5f : 0.0f;
			VkDebugUtilsLabelEXT vkDebugUtilsLabelEXT =
			{
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
				.pLabelName = kGpuTimerNames[eGpuTimer].data(),
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

void ProfileManagerBase::GpuRead(int64_t iCommandBuffer, GpuTimers eStart, GpuTimers eEnd, bool bLatchShadowSample)
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
			int64_t iCurrentMicroseconds = static_cast<int64_t>(static_cast<float>(puiResults[1] - puiResults[0]) * gpInstanceManager->mVkPhysicalDeviceProperties.limits.timestampPeriod / 1000.0f);
			mGpuTimers[eGpuTimer].smoothedMicroseconds = iCurrentMicroseconds;

			if (bLatchShadowSample && eGpuTimer == kGpuTimerShadow)
			{
				++mGpuShadowSample.uiSequence;
				mGpuShadowSample.iCurrentMicroseconds = iCurrentMicroseconds;
			}
		}
	}
}
#endif // BT_CLIENT

void ProfileManagerBase::BootStart(BootTimers eBootTimer)
{
	if constexpr (kbProfiling)
	{
		mBootTimers[eBootTimer].startTimePoint = std::chrono::steady_clock::now();
	}
}

void ProfileManagerBase::BootStop(BootTimers eBootTimer)
{
	if constexpr (kbProfiling)
	{
		BootTimer& rBootTimer = mBootTimers[eBootTimer];
		rBootTimer.timeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - rBootTimer.startTimePoint);
	}
}

void ProfileManagerBase::BootLog()
{
	if constexpr (kbProfiling)
	{
		BootStop(kBootTimerTotal);

		for (int64_t i = 0; i < kBootTimerCount; ++i)
		{
			std::chrono::milliseconds durationMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(mBootTimers[i].timeNs);
			if (durationMilliseconds.count() > 10)
			{
				LOG(kDefault, kDebug, "{}: {} ms", kBootTimerNames[i], durationMilliseconds.count());
			}
		}
		LOG(kDefault, kDebug, "\n");
	}
}

void ProfileManagerBase::LogTimers()
{
	if constexpr (kbProfiling)
	{
		LOG(kDefault, kDebug, "");

		{
			std::lock_guard lock(mCpuTimerMutex);

			int64_t iCpuTimerCount = GetCpuTimerCount();
			for (int64_t i = 0; i < iCpuTimerCount; ++i)
			{
				CpuTimer& rCpuTimer = GetCpuTimer(i);
				LOG(kDefault, kDebug, "{}: {} ({}, {}) [{}]", GetCpuTimerName(i), rCpuTimer.smoothedMicroseconds.Current(), rCpuTimer.smoothedMicroseconds.Average(), rCpuTimer.smoothedMicroseconds.Max(), rCpuTimer.smoothedAllocations.Get());
			}
		}

		LOG(kDefault, kDebug, "");

#if defined(BT_CLIENT)
		int64_t iCommandBufferCount = gpSwapchainManager->mFramebuffers.size();
		for (int64_t i = 0; i < iCommandBufferCount; ++i)
		{
			GpuRead(i, kGpuTimerGlobal, kGpuTimerCount, false);
		}

		for (int64_t i = 0; i < kGpuTimerCount; ++i)
		{
			GpuTimer& rGpuTimer = mGpuTimers[i];
			LOG(kDefault, kDebug, "{}: {} ({}, {})", kGpuTimerNames[i], rGpuTimer.smoothedMicroseconds.Current(), rGpuTimer.smoothedMicroseconds.Average(), rGpuTimer.smoothedMicroseconds.Max());
		}

		LOG(kDefault, kDebug, "");
#endif // BT_CLIENT
	}
}

#if defined(BT_CLIENT)
void ProfileManagerBase::DumpTimers()
{
	if constexpr (kbProfiling && kbProfilingDump)
	{
		if (mpDumpLog == nullptr)
		{
			return;
		}

		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		if (now - mLastDumpTime < kProfileDumpInterval)
		{
			return;
		}
		mLastDumpTime = now;

		int64_t iMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - mDumpStartTime).count();

		mpDumpLog->Write("{},meta,0,Fps,{},,,", iMs, gpGraphics->mRendersInTheLastSecond.Get());
		mpDumpLog->Write("{},meta,1,FullUpdates,{},,,", iMs, mFullUpdatesInTheLastSecond.Get());
		mpDumpLog->Write("{},meta,2,InterpolateUpdates,{},,,", iMs, mInterpolateUpdatesInTheLastSecond.Get());

		for (int64_t i = 0; i < kGpuTimerCount; ++i)
		{
			common::Smoothed<int64_t>& rMicroseconds = mGpuTimers[i].smoothedMicroseconds;
			mpDumpLog->Write("{},gpu,{},{},{},{},{},", iMs, i, kGpuTimerNames[i], rMicroseconds.Current(), rMicroseconds.Average(), rMicroseconds.Max());
		}

		// Snapshot under the lock, write after: DiagnosticLog flushes per line, and dozens of flushed lines under mCpuTimerMutex would block dispatch/submit/network CpuStart/CpuStop once per second, distorting the timers being measured.
		struct CpuTimerSample
		{
			int64_t iCurrentUs;
			int64_t iAverageUs;
			int64_t iMaxUs;
			int64_t iAllocations;
		};
		int64_t iCpuTimerCount = GetCpuTimerCount();
		auto pSamples = common::gpThreadLocal->mWorkbuffer.PushBuffer<CpuTimerSample*>(iCpuTimerCount * static_cast<int64_t>(sizeof(CpuTimerSample)));
		{
			std::lock_guard lock(mCpuTimerMutex);
			for (int64_t i = 0; i < iCpuTimerCount; ++i)
			{
				CpuTimer& rCpuTimer = GetCpuTimer(i);
				pSamples[i] = CpuTimerSample {rCpuTimer.smoothedMicroseconds.Current(), rCpuTimer.smoothedMicroseconds.Average(), rCpuTimer.smoothedMicroseconds.Max(), rCpuTimer.smoothedAllocations.Current()};
			}
		}
		for (int64_t i = 0; i < iCpuTimerCount; ++i)
		{
			mpDumpLog->Write("{},cpu,{},{},{},{},{},{}", iMs, i, GetCpuTimerName(i), pSamples[i].iCurrentUs, pSamples[i].iAverageUs, pSamples[i].iMaxUs, pSamples[i].iAllocations);
		}

		int64_t iCpuCounterCount = GetCpuCounterCount();
		for (int64_t i = 0; i < iCpuCounterCount; ++i)
		{
			mpDumpLog->Write("{},counter,{},{},{},,,", iMs, i, GetCpuCounterName(i), GetCpuCounter(i).iCount);
		}
	}
}
#endif // BT_CLIENT

void ProfileManagerBase::SmoothCpuTimers()
{
	if constexpr (kbProfiling)
	{
		std::lock_guard lock(mCpuTimerMutex);
		int64_t iCpuTimerCount = GetCpuTimerCount();
		for (int64_t i = 0; i < iCpuTimerCount; ++i)
		{
			CpuTimer& rCpuTimer = GetCpuTimer(i);
			if (!(rCpuTimer.flags & ProfileRowFlags::kSmoothAtStop))
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
}

void ProfileManagerBase::UpdateProfileText()
{
	if constexpr (kbProfiling)
	{
		ScopedCpuProfile scopedCpuProfile(kCpuTimerUpdateProfileText);

		SmoothCpuTimers();

		mSmoothedAllocations = giAllocationsThisFrame.exchange(0, std::memory_order_relaxed);
		mSmoothedAllocations.Update();

#if defined(BT_CLIENT)
		for (GpuTimer& rGpuTimer : mGpuTimers)
		{
			rGpuTimer.smoothedMicroseconds.Update();
		}

		// Before the kOff early-out so the dump runs with the overlay hidden.
		if constexpr (kbProfilingDump)
		{
			DumpTimers();
		}

		if (meProfileScreen == ProfileScreen::kOff)
		{
			return;
		}

		bool bReevaluate = TickVisibilityCadence();

		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;

		if (meProfileScreen == ProfileScreen::kCpu || meProfileScreen == ProfileScreen::kGpu)
		{
			int64_t iTotalCpuTimeUs = GetCpuTimer(game::kCpuTimerFrameUpdate).smoothedMicroseconds.Get();
			FormatFpsHeader(rWorkbuffer, *this, iTotalCpuTimeUs);
		}

		if (meProfileScreen == ProfileScreen::kCpu)
		{
			FormatCpuScreen(rWorkbuffer, *this, bReevaluate);
		}

		if (meProfileScreen == ProfileScreen::kGpu)
		{
			FormatGpuScreen(rWorkbuffer, *this, bReevaluate);
		}

		FormatGameScreens(rWorkbuffer);
#endif // BT_CLIENT
	}
}

CpuCounter& ProfileManagerBase::GetCpuCounter(int64_t iIndex)
{
	return iIndex < kEngineCpuCounterCount ? mEngineCpuCounters[iIndex] : mpGameCpuCounters[iIndex - kEngineCpuCounterCount];
}

CpuTimer& ProfileManagerBase::GetCpuTimer(int64_t iIndex)
{
	return iIndex < kEngineCpuTimerCount ? mEngineCpuTimers[iIndex] : mpGameCpuTimers[iIndex - kEngineCpuTimerCount];
}

std::string_view ProfileManagerBase::GetCpuCounterName(int64_t iIndex)
{
	return iIndex < kEngineCpuCounterCount ? kEngineCpuCounterNames[iIndex] : mpGameCpuCounterNames[iIndex - kEngineCpuCounterCount];
}

std::string_view ProfileManagerBase::GetCpuTimerName(int64_t iIndex)
{
	return iIndex < kEngineCpuTimerCount ? kEngineCpuTimerNames[iIndex] : mpGameCpuTimerNames[iIndex - kEngineCpuTimerCount];
}

int64_t ProfileManagerBase::GetCpuCounterCount() const
{
	return miCpuCounterCount;
}

int64_t ProfileManagerBase::GetCpuTimerCount() const
{
	return miCpuTimerCount;
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
	if constexpr (kbProfiling) { gpProfileManager->CpuStop(miCpuTimer); }
}

} // namespace engine
