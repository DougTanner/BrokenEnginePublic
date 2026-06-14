#pragma once

// This header is Engine.h's first include (Engine.h:4) and must stay engine-include-free — its
// upstream closure is Common + ExternalHeaders only. Any engine include added here becomes an
// aggregation-order landmine.

namespace engine
{

enum class ProfileScreen : uint8_t
{
	kOff,
	kCpu,
	kGpu,
	kFrames,
	kNetwork,
	kCount,
};

enum class ProfileRowFlags : uint8_t
{
	kVisible      = 1 << 0,
	kSmoothAtStop = 1 << 1,
};

struct CpuCounter
{
	std::string_view name;
	int64_t iCount = 0;
	common::Flags<ProfileRowFlags> flags {};
};

struct CpuTimerThreadState
{
	std::chrono::steady_clock::time_point startTimePoint;
	int64_t iStartAllocations = 0;
};

struct CpuTimer
{
	std::string_view name;

	int64_t iThreads = 0;
	int64_t iTotalFrameTimeNs = 0;

	int64_t iAllocationsThisFrame = 0;

	common::Smoothed<int64_t> smoothedMicroseconds {};
	common::Smoothed<int64_t> smoothedAllocations {};

	common::Flags<ProfileRowFlags> flags {};
};

enum EngineCpuCounters : int64_t
{
	kCpuCounterBillboards,
		kCpuCounterBillboardsRendered,
	kCpuCounterHexShields,
		kCpuCounterHexShieldsRendered,
	kCpuCounterAreaLights,
		kCpuCounterAreaLightsRendered,
	kCpuCounterPointLights,
		kCpuCounterPointLightsRendered,
	kCpuCounterPuffs,
		kCpuCounterPuffsRendered,
	kCpuCounterSmokeTrails,
		kCpuCounterSmokeTrailsRendered,
	kCpuCounterExplosions,
	kCpuCounterPushers,
	kCpuCounterSounds,
	kCpuCounterStreams,

	kEngineCpuCounterCount
};

enum EngineCpuTimers : int64_t
{
	kCpuTimerAcquireToGlobal,
	kCpuTimerRenderGlobal,
	kCpuTimerReduceInputLagFence,
	kCpuTimerAudio,
	kCpuTimerMessagesAndInput,
	kCpuTimerWaitFence,
	kCpuTimerRenderMain,
	kCpuTimerUpdateProfileText,
	kCpuTimerWaitPresentFuture,
	kCpuTimerSubmitGlobal,
	kCpuTimerSubmitImage,
	kCpuTimerPresent,
	kCpuTimerAcquireImage,
	kCpuTimerAcquireImageFence,
	kCpuTimerNetworkPollReconcile,
	kCpuTimerNetworkSend,

	kEngineCpuTimerCount
};

#if defined(BT_CLIENT)
enum GpuTimers : int64_t
{
	kGpuTimerGlobal,
		kGpuTimerShadow,
		kGpuTimerTerrainGen,
			kGpuTimerTerrainElevation,
		kGpuTimerSpread,
			kGpuTimerWindSpread,
			kGpuTimerSmokeSpread,
		kGpuTimerParticles,
			kGpuTimerParticlesSpawn,
			kGpuTimerLongParticlesUpdate,
			kGpuTimerSquareParticlesUpdate,
	kGpuTimerMain,
		kGpuTimerSmokeEmit,
		kGpuTimerWindDeposit,
		kGpuTimerLightingDeposit,
		kGpuTimerLightingSpread,
		kGpuTimerLightingCombine,
		kGpuTimerLightingTemporal,
		kGpuTimerObjectShadows,
		kGpuTimerObjectShadowsBlur,
		kGpuTimerWaterDisplacement,
		kGpuTimerWaterSkyboxOne,
	kGpuTimerImage,
		kGpuTimerObjects,
		kGpuTimerTransparentObjects,
		kGpuTimerTerrain,
		kGpuTimerWater,
		kGpuTimerHexShields,
		kGpuTimerText,
		kGpuTimerLongParticlesRender,
		kGpuTimerSquareParticlesRender,
		kGpuTimerVisibleLights,
		kGpuTimerBillboards,
	kGpuTimerUiRender,

	kGpuTimerCount
};

struct GpuTimer
{
	std::string_view name;
	common::Smoothed<int64_t> smoothedMicroseconds {};
	common::Flags<ProfileRowFlags> flags {};
};
#endif // BT_CLIENT

enum BootTimers : int64_t
{
	kBootTimerTotal,
		kBootTimerWaitForDataFile,
		kBootTimerWaitForTexturesFile,
		kBootTimerWaitForPriorityTextures,
		kBootTimerWaitForIslands,
		kBootTimerVulkan,
			kBootTimerInstanceManager,
			kBootTimerDeviceManager,
			kBootTimerSwapchainManager,
			kBootTimerParticleManager,
			kBootTimerPipelineManager,
			kBootTimerCommandBufferManager,
			kBootTimerBufferManager,
			kBootTimerIslands,
			kBootTimerTextureManager,
				kBootTimerTextureUpload,
				kModelTexturesGeneration,
			kBootTimerTextManager,
			kBootTimerRecordCommandBuffers,
			kBootTimerRenderPresent,

	kBootTimerCount
};

struct BootTimer
{
	std::string_view name;

	std::chrono::steady_clock::time_point startTimePoint {};
	std::chrono::nanoseconds timeNs {};
};

enum class CpuStopFlags : uint32_t
{
	kSmoothNow   = 0x01,
	kCrossThread = 0x02,
};
using CpuStopFlags_t = common::Flags<CpuStopFlags>;

class ProfileManagerBase
{
public:

	ProfileManagerBase() = default;
	virtual ~ProfileManagerBase() = default;

	void Create();
	void Destroy();

	void ToggleProfileText();

	void CpuStart(int64_t iCpuTimer, int64_t iThreads = 1);
	void CpuStop(int64_t iCpuTimer, CpuStopFlags_t flags = {});

	void SetCount(int64_t iCounter, int64_t iCount);

#if defined(BT_CLIENT)
	void ResetQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eStart, GpuTimers eEnd);

	void GpuStart(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGpuTimer);
	void GpuStop(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGpuTimer);
	void GpuRead(int64_t iCommandBuffer, GpuTimers eStart, GpuTimers eEnd);

	virtual void RenderImPlotGraphs() {}
#endif // BT_CLIENT

	void BootStart(BootTimers eBootTimer);
	void BootStop(BootTimers eBootTimer);
	void BootLog();

	void SmoothCpuTimers();
	void LogTimers();
	void UpdateProfileText();

	// Advances the shared profile-text visibility clock; returns true on a re-evaluation boundary (>= kProfileVisibilityInterval since last).
	bool TickVisibilityCadence();

	virtual CpuCounter& GetCpuCounter(int64_t iIndex);
	virtual CpuTimer& GetCpuTimer(int64_t iIndex);
	virtual int64_t GetCpuCounterCount() const;
	virtual int64_t GetCpuTimerCount() const;

	virtual void FormatGameScreens(common::Workbuffer&) {}

#if defined(BT_CLIENT)
	GpuTimer* GetGpuTimers() { return mGpuTimers; }
#endif // BT_CLIENT
	common::Smoothed<int64_t>& GetSmoothedAllocations() { return mSmoothedAllocations; }

	// One global mutex serializing all CPU timers: guards the CPU-timer arrays / per-thread timer states against concurrent writes from dispatch, submit (CommandBufferManager), and network threads. Designed for coarse phase scopes, not per-entity timers — finer granularity would distort the measurements it takes. Public so the engine::FormatCpuTimersText free function can lock the identical reads, matching the already-locked LogTimers.
	std::mutex mCpuTimerMutex;

	common::InTheLastSecond mFullUpdatesInTheLastSecond;
	common::InTheLastSecond mInterpolateUpdatesInTheLastSecond;

	ProfileScreen meProfileScreen = kbShowProfileTextByDefault ? ProfileScreen::kCpu : ProfileScreen::kOff;

#if defined(BT_SERVER) && !defined(ENABLE_CRT_DEBUG_HEAP)
	int64_t miMimallocCommittedMib = 0;
	int64_t miMimallocPeakCommittedMib = 0;
	int64_t miMimallocHeapUsedMib = 0;
	int64_t miMimallocPeakHeapUsedMib = 0;
#endif

protected:

	CpuCounter mEngineCpuCounters[kEngineCpuCounterCount]
	{
		{.name = "Billboards"},
		{.name = "    Rendered"},
		{.name = "HexShields"},
		{.name = "    Rendered"},
		{.name = "AreaLights"},
		{.name = "    Rendered"},
		{.name = "PointLights"},
		{.name = "    Rendered"},
		{.name = "Puffs"},
		{.name = "    Rendered"},
		{.name = "SmokeTrails"},
		{.name = "    Rendered"},
		{.name = "Explosions"},
		{.name = "Pushers"},
		{.name = "Sounds"},
		{.name = "Streams"},
	};

	CpuTimer mEngineCpuTimers[kEngineCpuTimerCount]
	{
		{.name = "Acquire to global"},
		{.name = "Render global"},
		{.name = "Reduce input lag fence"},
		{.name = "Audio"},
		{.name = "Messages and input"},
		{.name = "Wait fence"},
		{.name = "Render main"},
		{.name = "Profile text"},
		{.name = "Wait present future"},
		{.name = "    Submit global"},
		{.name = "    Submit image"},
		{.name = "    Present"},
		{.name = "Acquire image"},
		{.name = "    Fence"},
		{.name = "Network poll+reconcile"},
		{.name = "Network send"},
	};

#if defined(BT_CLIENT)
	GpuTimer mGpuTimers[kGpuTimerCount]
	{
		{.name = "Global render"},
		{.name = "    Shadow"},
		{.name = "    Terrain Gen"},
		{.name = "        Terrain Elevation"},
		{.name = "    Spread"},
		{.name = "        Wind Spread"},
		{.name = "        Smoke Spread"},
		{.name = "    Particles"},
		{.name = "        Particles Spawn"},
		{.name = "        Long Particles Update"},
		{.name = "        Square Particles Update"},
		{.name = "Main render"},
		{.name = "    Smoke Emit"},
		{.name = "    Wind Deposit"},
		{.name = "    Lighting Deposit"},
		{.name = "    Lighting Spread"},
		{.name = "    Lighting Combine"},
		{.name = "    Lighting Temporal"},
		{.name = "    Object Shadows"},
		{.name = "    Object Shadows blur"},
		{.name = "    Water Displacement"},
		{.name = "    Water Skybox One"},
		{.name = "Image render"},
		{.name = "    Objects"},
		{.name = "    Transparent Objects"},
		{.name = "    Terrain"},
		{.name = "    Water"},
		{.name = "    Hex Shields"},
		{.name = "    Text"},
		{.name = "    Long Particles Render"},
		{.name = "    Square Particles Render"},
		{.name = "    VisibleLights"},
		{.name = "    Billboards"},
		{.name = "Ui Render"},
	};
#endif // BT_CLIENT

	BootTimer mBootTimers[kBootTimerCount]
	{
		{.name = "Total"},
		{.name = "    Wait for data file"},
		{.name = "    Wait for textures file"},
		{.name = "    Wait for priority textures"},
		{.name = "    Wait for priority islands"},
		{.name = "    Vulkan"},
		{.name = "      InstanceManager"},
		{.name = "      DeviceManager"},
		{.name = "      SwapchainManager"},
		{.name = "      ParticleManager"},
		{.name = "      PipelineManager"},
		{.name = "      CommandBufferManager"},
		{.name = "      BufferManager"},
		{.name = "      Islands"},
		{.name = "      TextureManager"},
		{.name = "          TextureUpload"},
		{.name = "          Pbr textures"},
		{.name = "      TextManager"},
		{.name = "      Record command buffers"},
		{.name = "      Render present"},
	};

	std::chrono::steady_clock::time_point mLastVisibilityEvalTime {};

	std::unordered_map<std::thread::id, std::vector<CpuTimerThreadState>> mPerThreadTimerStates;

	common::Smoothed<int64_t> mSmoothedAllocations;

#if defined(BT_CLIENT)
	VkQueryPool mVkQueryPool = VK_NULL_HANDLE;
#endif // BT_CLIENT
};

class ScopedBootTimer
{
public:

	ScopedBootTimer(BootTimers eBootTimer);
	~ScopedBootTimer();
	ScopedBootTimer(const ScopedBootTimer&) = delete;
	ScopedBootTimer& operator=(const ScopedBootTimer&) = delete;

private:

	BootTimers meBootTimer;
};

class ScopedCpuProfile
{
public:

	ScopedCpuProfile(int64_t iCpuTimer, int64_t iThreads = 1);
	~ScopedCpuProfile();
	ScopedCpuProfile(const ScopedCpuProfile&) = delete;
	ScopedCpuProfile& operator=(const ScopedCpuProfile&) = delete;

private:

	int64_t miCpuTimer;
};

void FormatCpuTimersText(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, bool bReevaluate);
void FormatCpuCountersText(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, bool bReevaluate);

#if defined(BT_CLIENT)
void FormatFpsHeader(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, int64_t iTotalCpuTimeUs);
void FormatCpuScreen(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, bool bReevaluate);
void FormatGpuScreen(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager, bool bReevaluate);
#endif

} // namespace engine
