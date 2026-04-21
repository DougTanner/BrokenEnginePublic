#pragma once

#include "ExternalHeaders.h"

inline constexpr bool kbDesyncRecovery = false; // DT: TODO Need to properly test true;
inline constexpr bool kbFrameDispatch = true;
inline constexpr bool kbLogging = true;
inline constexpr bool kbQuadrantNeighborSubscriptions = true;
inline constexpr bool kbInvincibility = true;
inline constexpr bool kbRenderThread = true;

inline constexpr bool kbAlsoLogToPrintf = false;
inline constexpr bool kbFramebufferClearColor = false;
inline constexpr bool kbGpuAssistedValidation = false;
inline constexpr bool kbRecording = false;
inline constexpr bool kbRenderDocAttach = false;
inline constexpr bool kbScreenshots = false;
inline constexpr bool kbVulkanPipelineCache = false; // DT: TODO Need Crc and/or fallback if corrupt true;
inline constexpr bool kbWireframe = false;

#if defined(BT_SERVER)
inline constexpr bool kbSingleInstance = true;
#else
inline constexpr bool kbSingleInstance = false;
#endif

#if defined(BT_DEBUG)
// #define ENABLE_CRT_DEBUG_HEAP
inline constexpr const char* kpcBuildConfigName = "Debug";

inline constexpr bool kbAutoRunServer = false;
inline constexpr bool kbAutoConnect = true;
inline constexpr bool kbDebugBreak = true;
inline constexpr bool kbDebugInput = true;
inline constexpr bool kbDebugRender = true;
inline constexpr bool kbDxDiag = true;
inline constexpr bool kbProfiling = true;
inline constexpr bool kbProfilingFrameSpike = false;
inline constexpr bool kbReplayFullFrames = true;
inline constexpr bool kbVulkanDebugLayers = true;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = true;
inline constexpr bool kbShowProfileTextByDefault = false;
#elif defined(BT_PROFILE)
inline constexpr const char* kpcBuildConfigName = "Profile";

inline constexpr bool kbAutoRunServer = false;
inline constexpr bool kbAutoConnect = true;
inline constexpr bool kbDebugBreak = false;
inline constexpr bool kbDebugInput = false;
inline constexpr bool kbDebugRender = false;
inline constexpr bool kbDxDiag = false;
inline constexpr bool kbProfiling = true;
inline constexpr bool kbProfilingFrameSpike = true;
inline constexpr bool kbReplayFullFrames = false;
inline constexpr bool kbVulkanDebugLayers = false;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = false;
inline constexpr bool kbShowProfileTextByDefault = true;
#elif defined(BT_RELEASE)
inline constexpr const char* kpcBuildConfigName = "Release";

inline constexpr bool kbAutoRunServer = false;
inline constexpr bool kbAutoConnect = false;
inline constexpr bool kbDebugBreak = false;
inline constexpr bool kbDebugInput = false;
inline constexpr bool kbDebugRender = false;
inline constexpr bool kbDxDiag = true;
inline constexpr bool kbProfiling = false;
inline constexpr bool kbProfilingFrameSpike = false;
inline constexpr bool kbReplayFullFrames = false;
inline constexpr bool kbVulkanDebugLayers = false;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = false;
inline constexpr bool kbShowProfileTextByDefault = false;
#endif

#include "LogTypes.h"

inline constexpr LogLevel keLogLevelDefault  = kWarning;
inline constexpr LogLevel keLogLevelTemp     = kVerbose;

inline constexpr LogLevel keLogLevelAudio    = keLogLevelDefault;
inline constexpr LogLevel keLogLevelGraphics = keLogLevelDefault;
inline constexpr LogLevel keLogLevelLoading  = keLogLevelDefault;
inline constexpr LogLevel keLogLevelNavData  = keLogLevelDefault;
inline constexpr LogLevel keLogLevelNetwork  = kVerbose;
inline constexpr LogLevel keLogLevelInput    = keLogLevelDefault;

#include "Common.h"
#include "Shaders/ShaderLayouts.h"
#include "Ui/Wrapper.h"
#include "Frame/Frame.h"
#include "Engine.h"

inline constexpr engine::NetworkSimulationLevel keNetworkSimulation = engine::NetworkSimulationLevel::kDisabled;
