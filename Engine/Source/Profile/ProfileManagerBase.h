#pragma once

namespace engine
{

class DeviceManager;

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

	common::Smoothed<int64_t> smoothedMicroseconds;
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
	kCpuCounterTrails,
		kCpuCounterTrailsRendered,
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
		kGpuTimerSmokeSpread,
		kGpuTimerParticlesSpawn,
		kGpuTimerLongParticlesUpdate,
		kGpuTimerSquareParticlesUpdate,
	kGpuTimerMain,
		kGpuTimerSmokeEmit,
		kGpuTimerLighting,
		kGpuTimerLightingBlur,
		kGpuTimerLightingCombine,
		kGpuTimerObjectShadows,
		kGpuTimerObjectShadowsBlur,
	kGpuTimerImage,
		kGpuTimerObjects,
		kGpuTimerTerrain,
		kGpuTimerWater,
		kGpuTimerText,
		kGpuTimerLongParticlesRender,
		kGpuTimerSquareParticlesRender,
		kGpuTimerVisibleLights,
		kGpuTimerBillboards,

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
			kBootTimerShaderManager,
			kBootTimerSwapchainManager,
			kBootTimerParticleManager,
			kBootTimerPipelineManager,
			kBootTimerCommandBufferManager,
			kBootTimerBufferManager,
			kBootTimerIslands,
			kBootTimerTextureManager,
				kBootTimerTextureUpload,
				kGltfTexturesGeneration,
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

	void ResetGlobalQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer);
	void ResetMainQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer);

	void GpuStart(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGpuTimer);
	void GpuStop(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGGpuTimer);
	void GpuRead(int64_t iCommandBuffer, GpuTimers eStart, GpuTimers eEnd);

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

#if defined(BT_PROFILE)
	bool mbShowProfileText = true;
#else
	bool mbShowProfileText = false;
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
		{.name = "Trails"},
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
		GpuTimer { .name = "Global render" },
		GpuTimer { .name = "    Shadow" },
		GpuTimer { .name = "    Terrain Elevation" },
		GpuTimer { .name = "    Terrain Color" },
		GpuTimer { .name = "    Terrain Normal" },
		GpuTimer { .name = "    Terrain AO" },
		GpuTimer { .name = "    Smoke Spread" },
		GpuTimer { .name = "    Particles Spawn" },
		GpuTimer { .name = "    Long Particles Update" },
		GpuTimer { .name = "    Square Particles Update" },
		GpuTimer { .name = "Main render" },
		GpuTimer { .name = "    Smoke Emit" },
		GpuTimer { .name = "    Lighting" },
		GpuTimer { .name = "    Lighting Blur" },
		GpuTimer { .name = "    Lighting Combine" },
		GpuTimer { .name = "    Object Shadows" },
		GpuTimer { .name = "    Object Shadows blur" },
		GpuTimer { .name = "Image render" },
		GpuTimer { .name = "    Objects" },
		GpuTimer { .name = "    Terrain" },
		GpuTimer { .name = "    Water" },
		GpuTimer { .name = "    Text" },
		GpuTimer { .name = "    Long Particles Render" },
		GpuTimer { .name = "    Square Particles Render" },
		GpuTimer { .name = "    VisibleLights" },
		GpuTimer { .name = "    Billboards" },
	};

	BootTimer mBootTimers[kBootTimerCount]
	{
		BootTimer {.name = "Total" },
		BootTimer {.name = "    Wait for data file" },
		BootTimer {.name = "    Wait for textures file" },
		BootTimer {.name = "    Wait for priority textures" },
		BootTimer {.name = "    Wait for priority islands" },
		BootTimer {.name = "    Vulkan" },
		BootTimer {.name = "      InstanceManager" },
		BootTimer {.name = "      DeviceManager" },
		BootTimer {.name = "      ShaderManager" },
		BootTimer {.name = "      SwapchainManager" },
		BootTimer {.name = "      ParticleManager" },
		BootTimer {.name = "      PipelineManager" },
		BootTimer {.name = "      CommandBufferManager" },
		BootTimer {.name = "      BufferManager" },
		BootTimer {.name = "      Islands" },
		BootTimer {.name = "      TextureManager" },
		BootTimer {.name = "          TextureUpload" },
		BootTimer {.name = "          Gltf textures" },
		BootTimer {.name = "      TextManager" },
		BootTimer {.name = "      Record command buffers" },
		BootTimer {.name = "      Render present" },
	};

	VkQueryPool mVkQueryPool = VK_NULL_HANDLE;
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

} // namespace engine
