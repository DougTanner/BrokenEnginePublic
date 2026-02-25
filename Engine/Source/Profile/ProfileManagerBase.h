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
};

struct CpuTimer
{
	std::string_view name;

	std::chrono::high_resolution_clock::time_point startTimePoint;
	int64_t iThreads = 0;
	int64_t iTotalFrameTimeNs = 0;

	int64_t iStartAllocations = 0;
	int64_t iAllocationsThisFrame = 0;

	common::Smoothed<int64_t> smoothedMicroseconds;
	common::Smoothed<int64_t> smoothedAllocations;
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

	kEngineCpuTimerCount
};

enum GpuTimers : int64_t
{
	kGpuTimerGlobal,
		kGpuTimerShadow,
		kGpuTimerTerrainElevation,
		kGpuTimerTerrainColor,
		kGpuTimerTerrainNormal,
		kGpuTimerTerrainAmbientOcclusion,
		kGpuTimerWindSpread,
		kGpuTimerSmokeSpread,
		kGpuTimerParticlesSpawn,
		kGpuTimerLongParticlesUpdate,
		kGpuTimerSquareParticlesUpdate,
	kGpuTimerMain,
		kGpuTimerSmokeEmit,
		kGpuTimerWindDeposit,
		kGpuTimerLighting,
		kGpuTimerLightingBlur,
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
	common::Smoothed<int64_t> smoothedMicroseconds;
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

	std::chrono::high_resolution_clock::time_point startTimePoint;
	std::chrono::nanoseconds timeNs;
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
	void CpuStop(int64_t iCpuTimer, bool bSmoothNow);

	void SetCount(int64_t iCounter, int64_t iCount);

#ifdef BT_CLIENT
	void ResetGlobalQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer);
	void ResetMainQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer);
	void ResetUiQueryPool(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer);

	void GpuStart(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGpuTimer);
	void GpuStop(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGGpuTimer);
	void GpuRead(int64_t iCommandBuffer, GpuTimers eStart, GpuTimers eEnd);
#endif

	void BootStart(BootTimers eBootTimer);
	void BootStop(BootTimers eBootTimer);
	void BootLog();

	void LogTimers();
	void UpdateProfileText();

	virtual CpuCounter& GetCpuCounter(int64_t iIndex);
	virtual CpuTimer& GetCpuTimer(int64_t iIndex);
	virtual int64_t GetCpuCounterCount() const;
	virtual int64_t GetCpuTimerCount() const;

	common::InTheLastSecond mFullUpdatesInTheLastSecond;
	common::InTheLastSecond mInterpolateUpdatesInTheLastSecond;

	ProfileScreen meProfileScreen = kbShowProfileTextByDefault ? ProfileScreen::kCpu : ProfileScreen::kOff;

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
	};

	GpuTimer mGpuTimers[kGpuTimerCount]
	{
		{.name = "Global render"},
		{.name = "    Shadow"},
		{.name = "    Terrain Elevation"},
		{.name = "    Terrain Color"},
		{.name = "    Terrain Normal"},
		{.name = "    Terrain AO"},
		{.name = "    Wind Spread"},
		{.name = "    Smoke Spread"},
		{.name = "    Particles Spawn"},
		{.name = "    Long Particles Update"},
		{.name = "    Square Particles Update"},
		{.name = "Main render"},
		{.name = "    Smoke Emit"},
		{.name = "    Wind Deposit"},
		{.name = "    Lighting"},
		{.name = "    Lighting Blur"},
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

	common::Smoothed<int64_t> mSmoothedAllocations;

#ifdef BT_CLIENT
	VkQueryPool mVkQueryPool = VK_NULL_HANDLE;
#endif
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

extern thread_local int64_t giCpuProfilingSuppressed;

struct ScopedSuppressCpuProfiling
{
	ScopedSuppressCpuProfiling() { ++giCpuProfilingSuppressed; }
	~ScopedSuppressCpuProfiling() { --giCpuProfilingSuppressed; }
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

} // namespace engine
