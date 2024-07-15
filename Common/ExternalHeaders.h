// Disable warnings while parsing external headers
#pragma warning(push, 0)
#pragma warning(disable : 4702 6285 6323 6326 6385 6387 26051 26408 26429 26430 26432 26434 26435 26436 26438 26439 26440 26443 26446 26447 26451 26455 26456 26459 26460 26466 26472 26475 26481 26482 26485 26486 26489 26490 26493 26494 26495 26496 26497 26498 26812 26814 26818 26819)

// Debug defines
#if defined(BT_DEBUG)
	#undef DEBUG
	#undef _DEBUG
	#define DEBUG = 1
	#define _DEBUG = 1
	#undef NDEBUG
	#undef _NDEBUG
#else
	#undef NDEBUG
	#undef _NDEBUG
	#define NDEBUG = 1
	#define _NDEBUG = 1
	#undef DEBUG
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

// C
#include <float.h>

// C++
#if 0
// Also enable "Scan Source for Module Dependencies"
import std.core;
import std.filesystem;
import std.memory;
import std.threading;
using namespace std::chrono_literals;
#else
#include <algorithm>
#include <any>
#include <array>
// Do not use, very slow: #include <bitset>
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
#include <map>
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
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>
#endif

// Windows
#include <corecrt_math_defines.h>
#include <dxdiag.h>
#pragma comment(lib, "dxguid.lib")
#include <mmdeviceapi.h>
#include <ntverp.h>
#include <roapi.h>
#pragma comment(lib, "RuntimeObject.lib")
#include <ShlObj.h>
#include <windows.h>
#include <wrl/client.h>
#include <shellapi.h>

static_assert(VER_PRODUCTBUILD >= 10011 && VER_PRODUCTBUILD_QFE >= 16384, "Update the Windows SDK");

// DirectX Math, SSE only, no AVX because it's not deterministic (and SSE4 is actually slightly faster, for non-transcendentals anyway)
#define _XM_SSE4_INTRINSICS_
#include <DirectXMath.h>
#include <DirectXCollision.h>
namespace DirectX
{

XM_CONST float XM_PIDIV8 = DirectX::XM_PI / 8.0f;
XM_CONST float XM_PIDIV16 = DirectX::XM_PI / 16.0f;
XM_CONST float XM_PIDIV32 = DirectX::XM_PI / 32.0f;
XM_CONST float XM_PIDIV64 = DirectX::XM_PI / 64.0f;

}

#if defined(_XM_AVX_INTRINSICS_) || defined(_XM_AVX2_INTRINSICS_)
	#error
#endif

inline constexpr float kfEpsilon = 1.192092896e-7f; // DirectX::g_XMEpsilon

#define XMISNAN(x)  ((*(const uint32_t*)&(x) & 0x7F800000) == 0x7F800000 && (*(const uint32_t*)&(x) & 0x7FFFFF) != 0)
#define XMISINF(x)  ((*(const uint32_t*)&(x) & 0x7FFFFFFF) == 0x7F800000)

inline bool XM_CALLCONV operator==(DirectX::FXMVECTOR rOne, DirectX::FXMVECTOR rTwo)
{
	return DirectX::XMVector4Equal(rOne, rTwo);
}

inline bool operator==(const DirectX::XMFLOAT2& rOne, const DirectX::XMFLOAT2& rTwo)
{
	return rOne.x == rTwo.x && rOne.y == rTwo.y;
}

inline bool operator==(const DirectX::XMFLOAT3& rOne, const DirectX::XMFLOAT3& rTwo)
{
	return rOne.x == rTwo.x && rOne.y == rTwo.y && rOne.z == rTwo.z;
}

inline bool operator==(const DirectX::XMFLOAT4& rOne, const DirectX::XMFLOAT4& rTwo)
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

// PerlinNoise
#if defined(BT_ENGINE)
	#include "PerlinNoise/PerlinNoise.hpp"
#endif

// StackWalker
#if defined(BT_ENGINE)
	#include "StackWalker/Main/StackWalker/StackWalker.h"
#endif

// Vulkan
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan.h>
#if defined(BT_ENGINE)
	#pragma comment(lib, "vulkan-1.lib")
#endif
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
