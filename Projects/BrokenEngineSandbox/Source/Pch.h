#pragma once

inline constexpr bool kbEnableDesyncRecovery = false; // DT: TEMP true;
inline constexpr bool kbEnableFrameDispatch = true;
inline constexpr bool kbEnableLogging = true;
inline constexpr bool kbEnableQuadrantNeighborSubscriptions = true;
inline constexpr bool kbEnableRenderThread = true;
inline constexpr bool kbEnableReconcileThread = true;
inline constexpr bool kbEnableReconcileDispatch = true;

inline constexpr bool kbAlsoLogToPrintf = false;
inline constexpr bool kbEnableFramebufferClearColor = false;
inline constexpr bool kbEnableGpuAssistedValidation = false;
inline constexpr bool kbEnableRecording = false;
inline constexpr bool kbEnableScreenshots = false;
inline constexpr bool kbEnableWireframe = false;

// DT: TEMP GAMELOGIC
inline constexpr bool kbEnableSmokeSpreadTest = false;

#if defined(BT_SERVER)
inline constexpr bool kbSingleInstance = true;
#else
inline constexpr bool kbSingleInstance = false;
#endif

#if defined(BT_DEBUG)
// #define ENABLE_CRT_DEBUG_HEAP
inline constexpr const char* kpcBuildConfigName = "Debug";

inline constexpr bool kbEnableDebugBreak = true;
inline constexpr bool kbEnableDebugInput = true;
inline constexpr bool kbEnableDxDiag = true;
inline constexpr bool kbEnableInvincibility = true;
inline constexpr bool kbEnableProfiling = true;
inline constexpr bool kbEnableProfilingFrameSpike = false;
inline constexpr bool kbEnableReplayFullFrames = true;
inline constexpr bool kbEnableVulkanDebugLayers = true;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = true;
inline constexpr bool kbShowProfileTextByDefault = false;
#elif defined(BT_PROFILE)
inline constexpr const char* kpcBuildConfigName = "Profile";

inline constexpr bool kbEnableDebugBreak = false;
inline constexpr bool kbEnableDebugInput = false;
inline constexpr bool kbEnableDxDiag = false;
inline constexpr bool kbEnableInvincibility = true;
inline constexpr bool kbEnableProfiling = true;
inline constexpr bool kbEnableProfilingFrameSpike = true;
inline constexpr bool kbEnableReplayFullFrames = false;
inline constexpr bool kbEnableVulkanDebugLayers = false;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = false;
inline constexpr bool kbShowProfileTextByDefault = true;
#elif defined(BT_RELEASE)
inline constexpr const char* kpcBuildConfigName = "Release";

inline constexpr bool kbEnableDebugBreak = false;
inline constexpr bool kbEnableDebugInput = false;
inline constexpr bool kbEnableDxDiag = true;
inline constexpr bool kbEnableInvincibility = false;
inline constexpr bool kbEnableProfiling = false;
inline constexpr bool kbEnableProfilingFrameSpike = false;
inline constexpr bool kbEnableReplayFullFrames = false;
inline constexpr bool kbEnableVulkanDebugLayers = false;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = false;
inline constexpr bool kbShowProfileTextByDefault = false;
#endif

#include "ExternalHeaders.h"
#include "Common.h"
#include "Shaders/ShaderLayouts.h"
#include "Ui/Wrapper.h"
#include "Frame/Frame.h"
#include "Engine.h"

inline constexpr engine::NetworkSimulationLevel keNetworkSimulation = engine::NetworkSimulationLevel::kChina;
