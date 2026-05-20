#pragma once

namespace engine
{

class DeviceManager;

enum class ProfileScreen : uint8_t
{
	kOff,
	kCpu,
	kGpu,
	kFrames,
	kNetwork,
	kCount,
};

struct CpuCounter
{
	std::string_view name;
	int64_t iCount = 0;
	std::chrono::steady_clock::time_point lastVisibleTime {};
};

struct CpuTimerThreadState
{
	std::chrono::high_resolution_clock::time_point startTimePoint;
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

	std::chrono::steady_clock::time_point lastVisibleTime {};
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
		kGpuTimerObjectShadows,
		kGpuTimerObjectShadowsBlur,
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
	std::chrono::steady_clock::time_point lastVisibleTime {};
};

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

	std::chrono::high_resolution_clock::time_point startTimePoint {};
	std::chrono::nanoseconds timeNs {};
};

class ProfileManagerBase
{
public:

	ProfileManagerBase();
	virtual ~ProfileManagerBase();

	void Create();
	void Destroy();

	void ToggleProfileText();

	void CpuStart(int64_t iCpuTimer, int64_t iThreads = 1);
	void CpuStop(int64_t iCpuTimer, bool bSmoothNow, bool bCrossThread = false);

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

	virtual CpuCounter& GetCpuCounter(int64_t iIndex);
	virtual CpuTimer& GetCpuTimer(int64_t iIndex);
	virtual int64_t GetCpuCounterCount() const;
	virtual int64_t GetCpuTimerCount() const;

	virtual void FormatGameScreens(common::Workbuffer&) {}

	GpuTimer* GetGpuTimers() { return mGpuTimers; }
	common::Smoothed<int64_t>& GetSmoothedAllocations() { return mSmoothedAllocations; }

	common::InTheLastSecond mFullUpdatesInTheLastSecond;
	common::InTheLastSecond mInterpolateUpdatesInTheLastSecond;

	ProfileScreen meProfileScreen = kbShowProfileTextByDefault ? ProfileScreen::kCpu : ProfileScreen::kOff;

#if !defined(ENABLE_CRT_DEBUG_HEAP)
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
		{.name = "    Object Shadows"},
		{.name = "    Object Shadows blur"},
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

	std::mutex mCpuTimerMutex;
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

void FormatCpuTimersText(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager);
void FormatCpuCountersText(common::Workbuffer& rWorkbuffer, ProfileManagerBase& rProfileManager);

} // namespace engine
