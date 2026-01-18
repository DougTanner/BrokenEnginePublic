// Disable warnings while parsing external headers
#pragma warning(push, 0)
#pragma warning(disable : 4100 4702 6285 6323 6326 6385 6387 26051 26408 26429 26430 26432 26434 26435 26436 26438 26439 26440 26443 26446 26447 26451 26455 26456 26459 26460 26466 26472 26475 26481 26482 26485 26486 26489 26490 26493 26494 26495 26496 26497 26498 26812 26814 26818 26819 28251)

// Debug defines
#if defined(BT_DEBUG)
	#undef DEBUG
	#define DEBUG = 1
	#undef NDEBUG
	#undef _DEBUG
	#define _DEBUG = 1
	#undef _NDEBUG
#else
	#undef NDEBUG
	#define NDEBUG = 1
	#undef DEBUG
	#undef _NDEBUG
	#define _NDEBUG = 1
	#undef _DEBUG
#endif

// Visual Studio
#define _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR // https://stackoverflow.com/questions/78598141/first-stdmutexlock-crashes-in-application-built-with-latest-visual-studio
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _CRT_SECURE_NO_WARNINGS

// Windows headers
#define WINAPI_FAMILY WINAPI_FAMILY_DESKTOP_APP
#define _WIN32_WINNT _WIN32_WINNT_WIN10
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NODRAWTEXT
#define NOBITMAP
#define NOMCX
#define NOSERVICE
#define NOHELP

// Memory leak tracking
#if defined(DEBUG) || defined(_DEBUG)
	#define _CRTDBG_MAP_ALLOC
#endif

#if defined(_CRTDBG_MAP_ALLOC)
	#include <crtdbg.h>
#endif

// C++
#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
using namespace std::chrono_literals;
#include <codecvt>
#include <concepts>
#include <cstddef>
#include <deque>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <istream>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <ostream>
#include <queue>
#include <random>
#include <ranges>
#include <ratio>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Windows
#include <windows.h>

#include <corecrt_math_defines.h>
#include <dxdiag.h>
#pragma comment(lib, "dxguid.lib")
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ntverp.h>
#include <roapi.h>
#pragma comment(lib, "RuntimeObject.lib")
#include <ShlObj.h>
#include <wrl/client.h>
#include <shellapi.h>

static_assert(VER_PRODUCTBUILD >= 10011 && VER_PRODUCTBUILD_QFE >= 16384, "Update the Windows SDK");

// Make sure this is the first DirectXMath include location (can be included from other windows headers automatically)
#if defined(DIRECTX_MATH_VERSION)
	#error
#endif
// DirectX Math, SSE only, no AVX because it's not deterministic (and SSE4 is actually slightly faster, for non-transcendentals anyway)
#define _XM_SSE4_INTRINSICS_
#include <DirectXMath.h>
#include <DirectXCollision.h>
#if defined(_XM_AVX_INTRINSICS_) || defined(_XM_AVX2_INTRINSICS_)
	#error
#endif

namespace DirectX
{

constexpr float XM_PIDIV8  = XM_PI / 8.0f;
constexpr float XM_PIDIV16 = XM_PI / 16.0f;
constexpr float XM_PIDIV32 = XM_PI / 32.0f;
constexpr float XM_PIDIV64 = XM_PI / 64.0f;

}
using namespace DirectX;

inline constexpr float kfEpsilon = 1.192092896e-7f; // g_XMEpsilon

#define XMISNAN(x)  ((*(const uint32_t*)&(x) & 0x7F800000) == 0x7F800000 && (*(const uint32_t*)&(x) & 0x7FFFFF) != 0)
#define XMISINF(x)  ((*(const uint32_t*)&(x) & 0x7FFFFFFF) == 0x7F800000)

inline bool XM_CALLCONV operator==(FXMVECTOR rOne, FXMVECTOR rTwo)
{
	return XMVector4Equal(rOne, rTwo);
}

inline bool operator==(const XMFLOAT2& rOne, const XMFLOAT2& rTwo)
{
	return rOne.x == rTwo.x && rOne.y == rTwo.y;
}

inline bool operator==(const XMFLOAT3& rOne, const XMFLOAT3& rTwo)
{
	return rOne.x == rTwo.x && rOne.y == rTwo.y && rOne.z == rTwo.z;
}

inline bool operator==(const XMFLOAT4& rOne, const XMFLOAT4& rTwo)
{
	return rOne.x == rTwo.x && rOne.y == rTwo.y && rOne.z == rTwo.z && rOne.w == rTwo.w;
}

// DirectXTK
#if defined(BT_ENGINE)
	#include "DirectXTK/Inc/Audio.h"
	// Use XINPUT because it's supported on the Steam Deck
	#define USING_XINPUT
	#include "DirectXTK/Inc/GamePad.h"
	#include "DirectXTK/Inc/Mouse.h"
#endif

// Dear ImGui
#if defined(BT_ENGINE)
	#define VK_NO_PROTOTYPES
	#define VK_USE_PLATFORM_WIN32_KHR

	#define IMGUI_DEFINE_MATH_OPERATORS
	#define IMGUI_IMPL_VULKAN_USE_VOLK
	#define IMGUI_IMPL_VULKAN_VOLK_FILENAME <volk/volk.h>

	#include "imgui.h"
	#include "backends/imgui_impl_win32.h"
	#include "backends/imgui_impl_vulkan.h"
#endif

// PerlinNoise
#if defined(BT_ENGINE)
	#include "PerlinNoise/PerlinNoise.hpp"
#endif

// StackWalker
#if defined(BT_ENGINE)
	#include "StackWalker/Main/StackWalker/StackWalker.h"
#endif

// Vulkan - Using Volk meta-loader for direct driver access
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <volk/volk.h>
#include <vma/vk_mem_alloc.h>
#undef VK_NULL_HANDLE
#define VK_NULL_HANDLE nullptr

// Re-enable warnings after external headers
#pragma warning(pop)

// Sanity check to make sure DEBUG/NDEBUG/_DEBUG/_NDEBUG are defined correctly
#if defined(BT_DEBUG) && (!defined(DEBUG) || !defined(_DEBUG) || defined(NDEBUG) || defined(_NDEBUG))
	#error
#elif !defined(BT_DEBUG) && (defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG) || !defined(_NDEBUG))
	#error
#endif
