#pragma once

#include "Profile/GameProfile.h"

namespace engine
{

class DeviceManager;

struct CpuCounter
{
	std::string_view name;
	int64_t iCount = 0;
};

enum CpuCounters
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
	CPU_COUNTERS_GAME_ENUM

	kCpuCounterCount
};
inline CpuCounter gpCpuCounters[]
{
	CpuCounter {.name = "Billboards" },
	CpuCounter {.name = "    Rendered" },
	CpuCounter {.name = "HexShields" },
	CpuCounter {.name = "    Rendered" },
	CpuCounter {.name = "AreaLights" },
	CpuCounter {.name = "    Rendered" },
	CpuCounter {.name = "PointLights" },
	CpuCounter {.name = "    Rendered" },
	CpuCounter {.name = "Puffs" },
	CpuCounter {.name = "    Rendered" },
	CpuCounter {.name = "Trails" },
	CpuCounter {.name = "    Rendered" },
	CpuCounter {.name = "Explosions" },
	CpuCounter {.name = "Pushers" },
	CpuCounter {.name = "Sounds" },
	CPU_COUNTERS_GAME
};
static_assert(std::size(gpCpuCounters) == kCpuCounterCount);

struct CpuTimer
{
	std::string_view name;

	std::chrono::high_resolution_clock::time_point startTimePoint;
	int64_t iThreads = 0;
	int64_t iTotalFrameTimeNs = 0;

	common::Smoothed<int64_t> smoothedMicroseconds;
};

enum CpuTimers
{
	kCpuTimerAcquireToGlobal,

	kCpuTimerRenderGlobal,
	kCpuTimerReduceInputLagFence,
	kCpuTimerAudio,
	kCpuTimerMessagesAndInput,
	CPU_TIMERS_GAME_ENUM
	kCpuTimerWaitFence,
	kCpuTimerRenderMain,
	kCpuTimerUpdateProfileText,
	kCpuTimerWaitPresentFuture,
		kCpuTimerSubmitGlobal,
		kCpuTimerSubmitImage,
		kCpuTimerPresent,
	kCpuTimerAcquireImage,
		kCpuTimerAcquireImageFence,

	kCpuTimerCount
};
inline CpuTimer gpCpuTimers[]
{
	CpuTimer {.name = "Acquire to global" },

	CpuTimer {.name = "Render global" },
	CpuTimer {.name = "Reduce input lag fence" },
	CpuTimer {.name = "Audio" },
	CpuTimer {.name = "Messages and input" },
	CPU_TIMERS_GAME
	CpuTimer {.name = "Wait fence" },
	CpuTimer {.name = "Render main" },
	CpuTimer {.name = "Profile text" },
	CpuTimer {.name = "Wait present future"},
	CpuTimer {.name = "    Submit global" },
	CpuTimer {.name = "    Submit image" },
	CpuTimer {.name = "    Present"},
	CpuTimer {.name = "Acquire image" },
	CpuTimer {.name = "    Fence" },
};
static_assert(std::size(gpCpuTimers) == kCpuTimerCount);

enum GpuTimers
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
inline GpuTimer gpGpuTimers[]
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
static_assert(std::size(gpGpuTimers) == kGpuTimerCount);

enum BootTimers
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
inline BootTimer gpBootTimers[]
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
static_assert(std::size(gpBootTimers) == kBootTimerCount);

#if defined(ENABLE_PROFILING)

class ProfileManager
{
public:

	ProfileManager();
	~ProfileManager();

	void Create();
	void Destroy();

	void ToggleProfileText();

	void CpuStart(CpuTimers eCpuTimer, int64_t iThreads);
	void CpuStop(CpuTimers eCpuTimer, bool bSmoothNow);

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

	common::InTheLastSecond mFullUpdatesInTheLastSecond;
	common::InTheLastSecond mInterpolateUpdatesInTheLastSecond;

#if defined(BT_PROFILE)
	bool mbShowProfileText = true;
#else
	bool mbShowProfileText = false;
#endif

private:

	VkQueryPool mVkQueryPool = VK_NULL_HANDLE;
};

inline ProfileManager* gpProfileManager = nullptr;

#endif // ENABLE_PROFILING

} // namespace engine
