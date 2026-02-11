#pragma once

inline constexpr bool kbEnableLogging = true;
inline constexpr bool kbEnableRenderThread = true;

inline constexpr bool kbAlsoLogToPrintf = false;
inline constexpr bool kbEnableFramebufferClearColor = false;
inline constexpr bool kbEnableRecording = false;
inline constexpr bool kbEnableScreenshots = false;
inline constexpr bool kbEnableWireframe = false;

#if defined(BT_DEBUG)
// #define ENABLE_CRT_DEBUG_HEAP
inline constexpr bool kbEnableDebugBreak = true;
inline constexpr bool kbEnableDebugInput = true;
inline constexpr bool kbEnableDxDiag = true;
inline constexpr bool kbEnableGpuAssistedValidation = false;
inline constexpr bool kbEnableInvincibility = true;
inline constexpr bool kbEnableProfiling = true;
inline constexpr bool kbEnableReplayFullFrames = true;
inline constexpr bool kbEnableVulkanDebugLayers = true;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = true;
inline constexpr bool kbShowProfileTextByDefault = false;
#elif defined(BT_PROFILE)
inline constexpr bool kbEnableDebugBreak = true;
inline constexpr bool kbEnableDebugInput = true;
inline constexpr bool kbEnableDxDiag = false;
inline constexpr bool kbEnableGpuAssistedValidation = false;
inline constexpr bool kbEnableInvincibility = true;
inline constexpr bool kbEnableProfiling = true;
inline constexpr bool kbEnableReplayFullFrames = false;
inline constexpr bool kbEnableVulkanDebugLayers = false;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = false;
inline constexpr bool kbShowProfileTextByDefault = true;
#elif defined(BT_RELEASE)
inline constexpr bool kbEnableDebugBreak = false;
inline constexpr bool kbEnableDebugInput = false;
inline constexpr bool kbEnableDxDiag = true;
inline constexpr bool kbEnableGpuAssistedValidation = false;
inline constexpr bool kbEnableInvincibility = false;
inline constexpr bool kbEnableProfiling = false;
inline constexpr bool kbEnableReplayFullFrames = false;
inline constexpr bool kbEnableVulkanDebugLayers = false;
inline constexpr bool kbRandomlyInvalidatePbrCubemapCache = false;
inline constexpr bool kbShowProfileTextByDefault = false;
#endif

#include "ExternalHeaders.h"
#include "Utils.h"
#include "Shaders/ShaderLayouts.h"
#include "Ui/Wrapper.h"
