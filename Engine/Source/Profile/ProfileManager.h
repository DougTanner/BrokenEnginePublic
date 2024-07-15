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
	kCpuCounterLights,
		kCpuCounterAreaLightsRendered,
		kCpuCounterPointLightsRendered,
	kCpuCounterSmokePuffs,
		kCpuCounterSmokePuffsRendered,
	kCpuCounterSmokeTrails,
		kCpuCounterSmokeTrailsRendered,
	kCpuCounterControllers,
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
	CpuCounter {.name = "Lights" },
	CpuCounter {.name = "    Area rendered" },
	CpuCounter {.name = "    Point rendered" },
	CpuCounter {.name = "Smoke puffs" },
	CpuCounter {.name = "    Rendered" },
	CpuCounter {.name = "Smoke trails" },
	CpuCounter {.name = "    Rendered" },
	CpuCounter {.name = "Controllers" },
	CpuCounter {.name = "Explosions" },
	CpuCounter {.name = "Pushers" },
	CpuCounter {.name = "Sounds" },
	CPU_COUNTERS_GAME
};
static_assert(std::size(gpCpuCounters) == kCpuCounterCount);

struct CpuTimer
{
	std::string_view pcName;

	std::chrono::high_resolution_clock::time_point startTimePoint;
	int64_t iThreads = 0;
	int64_t iTotalFrameTimeNs = 0;

	common::Smoothed<int64_t> smoothedMicroseconds;
};

enum CpuTimers
{
	kCpuTimerAcquireToGlobal,

	kCpuTimerFrameGlobal,
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
		kCpuTimerSubmitMain,
		kCpuTimerSubmitImage,
		kCpuTimerPresent,
	kCpuTimerAcquireImage,
		kCpuTimerAcquireImageFence,

	kCpuTimerCount
};
inline CpuTimer gpCpuTimers[]
{
	CpuTimer {.pcName = "Acquire to global" },

	CpuTimer {.pcName = "Frame global" },
	CpuTimer {.pcName = "Render global" },
	CpuTimer {.pcName = "Reduce input lag fence" },
	CpuTimer {.pcName = "Audio" },
	CpuTimer {.pcName = "Messages and input" },
	CPU_TIMERS_GAME
	CpuTimer {.pcName = "Wait fence" },
	CpuTimer {.pcName = "Render main" },
	CpuTimer {.pcName = "Profile text" },
	CpuTimer {.pcName = "Wait present future"},
	CpuTimer {.pcName = "    Submit global" },
	CpuTimer {.pcName = "    Submit main" },
	CpuTimer {.pcName = "    Submit image" },
	CpuTimer {.pcName = "    Present"},
	CpuTimer {.pcName = "Acquire image" },
	CpuTimer {.pcName = "    Fence" },
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
		kGpuTimerWidgets,
		kGpuTimerLongParticlesRender,
		kGpuTimerSquareParticlesRender,
		kGpuTimerVisibleLights,
		kGpuTimerBillboards,

	kGpuTimerCount
};
struct GpuTimer
{
	std::string_view pcName;
	common::Smoothed<int64_t> smoothedMicroseconds;
};
inline GpuTimer gpGpuTimers[]
{
	GpuTimer { .pcName = "Global render" },
	GpuTimer { .pcName = "    Shadow" },
	GpuTimer { .pcName = "    Terrain Elevation" },
	GpuTimer { .pcName = "    Terrain Color" },
	GpuTimer { .pcName = "    Terrain Normal" },
	GpuTimer { .pcName = "    Terrain AO" },
	GpuTimer { .pcName = "    Smoke Spread" },
	GpuTimer { .pcName = "    Particles Spawn" },
	GpuTimer { .pcName = "    Long Particles Update" },
	GpuTimer { .pcName = "    Square Particles Update" },
	GpuTimer { .pcName = "Main render" },
	GpuTimer { .pcName = "    Smoke Emit" },
	GpuTimer { .pcName = "    Lighting" },
	GpuTimer { .pcName = "    Lighting Blur" },
	GpuTimer { .pcName = "    Lighting Combine" },
	GpuTimer { .pcName = "    Object Shadows" },
	GpuTimer { .pcName = "    Object Shadows blur" },
	GpuTimer { .pcName = "Image render" },
	GpuTimer { .pcName = "    Objects" },
	GpuTimer { .pcName = "    Terrain" },
	GpuTimer { .pcName = "    Water" },
	GpuTimer { .pcName = "    Text" },
	GpuTimer { .pcName = "    Widgets" },
	GpuTimer { .pcName = "    Long Particles Render" },
	GpuTimer { .pcName = "    Square Particles Render" },
	GpuTimer { .pcName = "    VisibleLights" },
	GpuTimer { .pcName = "    Billboards" },
};
static_assert(std::size(gpGpuTimers) == kGpuTimerCount);

enum BootTimers
{
	kBootTimerTotal,
		kBootTimerWaitForDataFile,
		kBootTimerWaitForTexturesFile,
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
			kBootTimerBuildGlobalHeightmap,
			kBootTimerRecordCommandBuffers,

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
	BootTimer {.name = "    Vulkan" },
	BootTimer {.name = "      InstanceManager" },
	BootTimer {.name = "      DeviceManager" },
	BootTimer {.name = "      ShaderManager" },
	BootTimer {.name = "      ParticleManager" },
	BootTimer {.name = "      SwapchainManager" },
	BootTimer {.name = "      PipelineManager" },
	BootTimer {.name = "      CommandBufferManager" },
	BootTimer {.name = "      BufferManager" },
	BootTimer {.name = "      Islands" },
	BootTimer {.name = "      TextureManager" },
	BootTimer {.name = "          TextureUpload" },
	BootTimer {.name = "          Gltf textures generation" },
	BootTimer {.name = "      TextManager" },
	BootTimer {.name = "      Build global heightmap" },
	BootTimer {.name = "      Record command buffers" },
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
	void ResetImageQueryPools(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer);

	void GpuStart(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGpuTimer);
	void GpuStop(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, GpuTimers eGGpuTimer);
	void GpuRead(int64_t iCommandBuffer, GpuTimers eStart, GpuTimers eEnd);

	void BootStart(BootTimers eBootTimer);
	void BootStop(BootTimers eBootTimer);
	void BootLog();

	void LogTimers();
	void UpdateProfileText();

	common::InTheLastSecond mUpdatesInTheLastSecond;

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
