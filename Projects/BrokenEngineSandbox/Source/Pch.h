#pragma once

#define ENABLE_LOGGING
#define ENABLE_RENDER_THREAD

#if defined(BT_DEBUG)
	#define ENABLE_DEBUG_INPUT
	#define ENABLE_REPLAY_FULL_FRAMES
	#define ENABLE_PROFILING
	#define ENABLE_VULKAN_DEBUG_LAYERS
	// 40 fps #define ENABLE_GPU_ASSISTED_VALIDATION
	#define ENABLE_INVINCIBILITY
#endif

#if defined(BT_PROFILE)
	#define ENABLE_PROFILING
#endif

#if !defined(BT_PROFILE)
	#define ENABLE_DXDIAG
#endif

// #define ENABLE_RECORDING
#if defined(ENABLE_RECORDING)
	#define ENABLE_DEBUG_INPUT
	#define ENABLE_INVINCIBILITY
#endif

// #define ENABLE_WIREFRAME
// #define ENABLE_FRAMEBUFFER_CLEAR_COLOR
// #define ENABLE_GLTF_TEST
// #define ENABLE_SCREENSHOTS
// #define ENABLE_NAVMESH_DISPLAY

#include "ExternalHeaders.h"

#include "Defines.h"

#include "Utils.h"

#include "Ui/Wrapper.h"

#include "Shaders/ShaderLayouts.h"

#include "Data/Data.h"
