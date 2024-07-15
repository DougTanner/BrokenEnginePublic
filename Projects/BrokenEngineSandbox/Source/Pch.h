#pragma once

#define ENABLE_LOGGING
#define ENABLE_DXDIAG
#define ENABLE_RENDER_THREAD

// #define ENABLE_DEBUG_INPUT

#if defined(BT_DEBUG)
	#define ENABLE_DEBUG_INPUT
	#define ENABLE_PROFILING
	#define ENABLE_VULKAN_DEBUG_LAYERS
	#define ENABLE_INVINCIBILITY
#endif

#if defined(BT_PROFILE)
	#define ENABLE_PROFILING
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

#include "Shaders/ShaderLayouts.h"

#include "../Platforms/VisualStudio2022/Output/Data.h"
