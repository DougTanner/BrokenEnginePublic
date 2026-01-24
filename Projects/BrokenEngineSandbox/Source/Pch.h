#pragma once

inline constexpr bool kbEnableLogging = true;
inline constexpr bool kbEnableRenderThread = true;
inline constexpr bool kbEnableRecording = false;
inline constexpr bool kbEnableWireframe = false;
inline constexpr bool kbEnableFramebufferClearColor = false;
inline constexpr bool kbEnableScreenshots = false;
inline constexpr bool kbAlsoLogToPrintf = false;

#if defined(BT_DEBUG)
inline constexpr bool kbEnableInvincibility = true;
inline constexpr bool kbEnableReplayFullFrames = true;
inline constexpr bool kbEnableProfiling = true;
inline constexpr bool kbEnableVulkanDebugLayers = true;
inline constexpr bool kbEnableGpuAssistedValidation = false;
inline constexpr bool kbEnableDebugInput = true;
inline constexpr bool kbEnableDxDiag = true;
inline constexpr bool kbEnableDebugBreak = true;
#elif defined(BT_PROFILE)
inline constexpr bool kbEnableInvincibility = true;
inline constexpr bool kbEnableReplayFullFrames = false;
inline constexpr bool kbEnableProfiling = true;
inline constexpr bool kbEnableVulkanDebugLayers = false;
inline constexpr bool kbEnableGpuAssistedValidation = false;
inline constexpr bool kbEnableDebugInput = true;
inline constexpr bool kbEnableDxDiag = false;
inline constexpr bool kbEnableDebugBreak = true;
#elif defined(BT_RELEASE)
inline constexpr bool kbEnableInvincibility = false;
inline constexpr bool kbEnableReplayFullFrames = false;
inline constexpr bool kbEnableProfiling = false;
inline constexpr bool kbEnableVulkanDebugLayers = false;
inline constexpr bool kbEnableGpuAssistedValidation = false;
inline constexpr bool kbEnableDebugInput = false;
inline constexpr bool kbEnableDxDiag = true;
inline constexpr bool kbEnableDebugBreak = false;
#endif

#include "ExternalHeaders.h"
#include "Utils.h"
#include "Shaders/ShaderLayouts.h"
#include "Data/Data.h"
#include "Ui/Wrapper.h"
