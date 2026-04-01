#pragma once

#include "ExternalHeaders.h"

inline constexpr bool kbDesyncRecovery = false; // DT: TEMP true;
inline constexpr bool kbFrameDispatch = true;
inline constexpr bool kbLogging = true;
inline constexpr bool kbQuadrantNeighborSubscriptions = true;
inline constexpr bool kbRenderThread = true;
inline constexpr bool kbReconcileThread = true;
inline constexpr bool kbReconcileDispatch = true;

inline constexpr bool kbAlsoLogToPrintf = false;
inline constexpr bool kbFramebufferClearColor = false;
inline constexpr bool kbGpuAssistedValidation = false;
inline constexpr bool kbRecording = false;
inline constexpr bool kbScreenshots = false;
inline constexpr bool kbWireframe = false;

// DT: TEMP GAMELOGIC
inline constexpr bool kbSmokeSpreadTest = false;

#if defined(BT_SERVER)
inline constexpr bool kbSingleInstance = true;
#else
inline constexpr bool kbSingleInstance = false;
#endif

#if defined(BT_DEBUG)
// #define ENABLE_CRT_DEBUG_HEAP
inline constexpr const char* kpcBuildConfigName = "Debug";

inline constexpr bool kbAutoServer = false; // DT: TEMP true;
inline constexpr bool kbDebugBreak = true;
inline constexpr bool kbDebugInput = true;
inline constexpr bool kbDxDiag = true;
inline constexpr bool kbInvincibility = true;
inline constexpr bool kbProfiling = true;
inline constexpr bool kbProfilingFrameSpike = false;
inline constexpr bool kbReplayFullFrames = true;
inline constexpr bool kbVulkanDebugLayers = true;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = true;
inline constexpr bool kbShowProfileTextByDefault = false;
#elif defined(BT_PROFILE)
inline constexpr const char* kpcBuildConfigName = "Profile";

inline constexpr bool kbAutoServer = true;
inline constexpr bool kbDebugBreak = false;
inline constexpr bool kbDebugInput = false;
inline constexpr bool kbDxDiag = false;
inline constexpr bool kbInvincibility = true;
inline constexpr bool kbProfiling = true;
inline constexpr bool kbProfilingFrameSpike = true;
inline constexpr bool kbReplayFullFrames = false;
inline constexpr bool kbVulkanDebugLayers = false;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = false;
inline constexpr bool kbShowProfileTextByDefault = true;
#elif defined(BT_RELEASE)
inline constexpr const char* kpcBuildConfigName = "Release";

inline constexpr bool kbAutoServer = false;
inline constexpr bool kbDebugBreak = false;
inline constexpr bool kbDebugInput = false;
inline constexpr bool kbDxDiag = true;
inline constexpr bool kbInvincibility = false;
inline constexpr bool kbProfiling = false;
inline constexpr bool kbProfilingFrameSpike = false;
inline constexpr bool kbReplayFullFrames = false;
inline constexpr bool kbVulkanDebugLayers = false;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = false;
inline constexpr bool kbShowProfileTextByDefault = false;
#endif

#include "Common.h"
#include "Shaders/ShaderLayouts.h"
#include "Ui/Wrapper.h"
#include "Frame/Frame.h"
#include "Engine.h"

inline constexpr engine::NetworkSimulationLevel keNetworkSimulation = engine::NetworkSimulationLevel::kChina;
inline constexpr uint64_t kLogEnabledCategoriesDefault = kLogDefault | kLogNetwork;
