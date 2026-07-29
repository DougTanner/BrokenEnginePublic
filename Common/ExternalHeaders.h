// Cross-CPU determinism: AVX/AVX2/AVX512 change scalar/SSE FP and std:: math codegen across the whole
// translation unit and are banned (see Documents/FloatingPointDeterminism.txt). Catch /arch:AVX* on the
// compiler command line up front, before any include can pull in intrinsics.
#if defined(__AVX__) || defined(__AVX2__) || defined(__AVX512F__)
	#error "AVX/AVX2/AVX512 detected - banned for cross-CPU determinism; remove /arch:AVX*"
#endif

// Disable all warnings while parsing external headers
#include <codeanalysis/warnings.h>
#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#endif

// Debug defines
#if defined(BT_DEBUG)
	#undef DEBUG
	#define DEBUG 1
	#undef NDEBUG
	#undef _DEBUG
	#define _DEBUG 1
	#undef _NDEBUG
#else
	#undef NDEBUG
	#define NDEBUG 1
	#undef DEBUG
	#undef _NDEBUG
	#define _NDEBUG 1
	#undef _DEBUG
#endif

// Visual Studio
#define _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR // https://stackoverflow.com/questions/78598141/first-stdmutexlock-crashes-in-application-built-with-latest-visual-studio
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
#if defined(ENABLE_CRT_DEBUG_HEAP)
	#define _CRTDBG_MAP_ALLOC
#endif

#if defined(_CRTDBG_MAP_ALLOC)
	#include <crtdbg.h>
#endif

// C++
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
using namespace std::chrono_literals;
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <istream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numbers>
#include <numeric>
#include <optional>
#include <ostream>
#include <queue>
#include <random>
#include <ratio>
#if defined(BT_ENGINE)
	#include <regex> // Runtime game agent command channel only (get_logs pattern match); kept out of the offline DataPacker and Common-only TUs.
#endif
#include <semaphore>
#include <source_location>
#include <span>
#include <variant>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Windows
#include <windows.h>

#include <corecrt_math.h>
#include <corecrt_math_defines.h>
#include <dxdiag.h>
#pragma comment(lib, "dxguid.lib")
#if !defined(BT_SERVER)
#include <mmdeviceapi.h>
#endif
#include <mmreg.h>
#include <timeapi.h>
#pragma comment(lib, "Winmm.lib")
#include <ntverp.h>
#include <roapi.h>
#pragma comment(lib, "RuntimeObject.lib")
#include <ShlObj.h>
#include <wrl/client.h>
#include <shellapi.h>

static_assert(VER_PRODUCTBUILD > 10011 || (VER_PRODUCTBUILD == 10011 && VER_PRODUCTBUILD_QFE >= 16384), "Update the Windows SDK");

// Make sure this is the first DirectXMath include location (can be included from other windows headers automatically)
#if defined(DIRECTX_MATH_VERSION)
	#error "DirectXMath included before ExternalHeaders.h - it must be the first include site so the SSE4-only determinism knob takes effect"
#endif
// DirectX Math, SSE only, no AVX because it's not deterministic (and SSE4 is actually slightly faster, for non-transcendentals anyway)
#define _XM_SSE4_INTRINSICS_
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <DirectXPackedVector.h>
#if defined(_XM_AVX_INTRINSICS_) || defined(_XM_AVX2_INTRINSICS_)
	#error "DirectXMath auto-promoted to AVX/AVX2 intrinsics - banned for cross-CPU determinism"
#endif

namespace DirectX
{

inline constexpr float XM_PIDIV8  = XM_PI / 8.0f;
inline constexpr float XM_PIDIV16 = XM_PI / 16.0f;
inline constexpr float XM_PIDIV32 = XM_PI / 32.0f;
inline constexpr float XM_PIDIV64 = XM_PI / 64.0f;
inline constexpr float XM_PIDIV128 = XM_PI / 128.0f;

}
using namespace DirectX;

inline constexpr float kfEpsilon = 1.192092896e-7f; // g_XMEpsilon

// NaN/Inf bit tests. Free functions (not macros) for single-evaluation and no strict-aliasing UB: std::bit_cast
// (constexpr, <bit>) replaces the type-punning reinterpret_cast. Named XmIsNan/XmIsInf to avoid colliding with
// DirectXMath's own XMISNAN/XMISINF macros. Owned by ExternalHeaders.h by design.
inline constexpr bool XmIsNan(float fValue)
{
	const uint32_t uiBits = std::bit_cast<uint32_t>(fValue);
	return (uiBits & 0x7F800000u) == 0x7F800000u && (uiBits & 0x007FFFFFu) != 0u;
}

inline constexpr bool XmIsInf(float fValue)
{
	return (std::bit_cast<uint32_t>(fValue) & 0x7FFFFFFFu) == 0x7F800000u;
}

// Deterministic exact (non-epsilon) IEEE operator== for XMVECTOR / XMFLOAT2/3/4 lives in Determinism.h
// (kept out of this header so it can sit alongside the MXCSR + exception-handler setup). This is value
// equality rather than byte identity. Included here, right after the DirectXMath includes, so the
// operators stay globally visible for all downstream code.
#include "Determinism.h"

// DirectXTK
#if defined(BT_CLIENT)
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
	#define VK_USE_64_BIT_PTR_DEFINES 1 // Non-dispatchable handles as pointers so Vulkan defines VK_NULL_HANDLE = nullptr natively (no SDK-macro override)

	#define IMGUI_DEFINE_MATH_OPERATORS
	#define IMGUI_IMPL_VULKAN_USE_VOLK
	#define IMGUI_IMPL_VULKAN_VOLK_FILENAME <Volk/volk.h>

	#pragma warning(push, 0)
	#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
	#ifdef __clang__
		#pragma clang diagnostic push
		#pragma clang diagnostic ignored "-Weverything"
	#endif
	#include "imgui.h"
	#include "backends/imgui_impl_win32.h"
	#include "backends/imgui_impl_vulkan.h"
	#include "implot.h"
	// Client-only: imgui internals for the agent UI registry (engine::AgentUiRegistry reads ImRect / ImGuiContext /
	// ImGuiWindow and implements the test-engine hook externs). IMGUI_ENABLE_TEST_ENGINE only exposes those extern
	// declarations + the ItemAdd/ItemInfo macros; the gating ImGuiContext member exists unconditionally, so the
	// client view matches the vendored library (compiled with the same define). Kept out of the server PCH.
	#if defined(BT_CLIENT)
		#define IMGUI_ENABLE_TEST_ENGINE
		#include "imgui_internal.h"
	#endif
	#ifdef __clang__
		#pragma clang diagnostic pop
	#endif
	#pragma warning(pop)

	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

// Mimalloc
#if defined(BT_ENGINE)
	#include <mimalloc.h>
	#if !defined(ENABLE_CRT_DEBUG_HEAP)
		#include <mimalloc-override.h>
		#include <mimalloc-stats.h>
	#endif
#endif

// PerlinNoise
#if defined(BT_CLIENT)
	#include "PerlinNoise/PerlinNoise.hpp"
#endif

// RenderDoc - in-application frame capture API (client-only; consumed by InstanceManager)
#if defined(BT_CLIENT)
	#include "RenderDoc/renderdoc_app.h"
#endif

// stb - image resize / write (client consumption only; implementation in the Prebuilts unity .cpp)
#if defined(BT_CLIENT)
	#include "stb/stb_image_resize2.h"
	#include "stb/stb_image_write.h"
#endif

// StackWalker
#include "StackWalker/Main/StackWalker/StackWalker.h"

// Vulkan - Using Volk meta-loader for direct driver access
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_64_BIT_PTR_DEFINES 1 // Non-dispatchable handles as pointers so Vulkan defines VK_NULL_HANDLE = nullptr natively (no SDK-macro override)
#include <Volk/volk.h>
#include <vma/vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h> // Generated enum-to-string helpers, ships with the LunarG SDK

// LZ4
#if defined(BT_ENGINE)
	#include "lz4/lib/lz4.h"
#endif
#if defined(BT_DATA_PACKER)
	// Offline texture-chunk compression uses LZ4HC at max level; the runtime only ever decompresses.
	// lz4hc.h declares LZ4_compress_HC / LZ4HC_CLEVEL_MAX; lz4.h declares LZ4_compressBound.
	#include "lz4/lib/lz4.h"
	#include "lz4/lib/lz4hc.h"
#endif

// zlib
#include "zlib/zlib.h"

// ENet
#if defined(BT_ENGINE)
	#include "enet/enet.h"
#endif

// Clipper2 - polygon clipping & offsetting (Boost Software License 1.0)
#if defined(BT_ENGINE)
	#include "clipper2/clipper.h"
	#include "clipper2/clipper.offset.h"
#endif

// nlohmann::json (vendored under tinygltf) - runtime agent command channel (BT_ENGINE) and offline island
// bake archetype/route loaders (BT_DATA_PACKER). The warning-suppression pragma travels with the header.
#if defined(BT_ENGINE) || defined(BT_DATA_PACKER)
	#pragma warning(push)
	#pragma warning(disable : 5311) // nlohmann::json 3.10.4 uses the pre-C++20 literal-operator-id form 'operator "" _json'
	#include "tinygltf/json.hpp"
	#pragma warning(pop)
#endif

// DataPacker-only third-party consumption headers (offline asset tooling). Centralized here, gated by
// BT_DATA_PACKER, so the consumption includes live in one place rather than scattered across the DataPacker
// .cpp/.h files. The library *implementation* units stay in the Prebuilts/Source/DataPacker unity .cpp's by
// necessity. Config defines/pragmas not covered by the global warning span above travel with their header.
#if defined(BT_ENGINE) || defined(BT_DATA_PACKER)
	// FileManager uses BCrypt to bind replay manifests to the exact persisted component bytes.
	#include <bcrypt.h>
	#pragma comment(lib, "bcrypt.lib")
#endif

#if defined(BT_DATA_PACKER)
	// Offline file traversal needs reparse-point FSCTL queries.
	#include <winioctl.h>

	// DirectXTK - WAV parsing
	#include "DirectXTK/Audio/WAVFileReader.h"

	// bc7enc_rdo - BCn encode/decode (consumption only; RGBCX/implementation defined in the Prebuilts unity .cpp)
	#include "bc7enc_rdo/bc7decomp.h"
	#include "bc7enc_rdo/rdo_bc_encoder.h"
	#include "bc7enc_rdo/rgbcx.h"

	// gli - texture image library (vendors glm)
	#define GLM_STATIC_ASSERT static_assert
	#include "gli/gli/gli.hpp"

	// cmft - cubemap filtering. Its headers don't parse with the CRT debug-heap malloc/realloc/free macros
	// (from ENABLE_CRT_DEBUG_HEAP); neutralize across the cmft includes, then restore so the rest of the tool
	// keeps leak tracking.
	#undef malloc
	#undef realloc
	#undef free
	#include <cmft/clcontext.h>
	#include <cmft/image.h>
	#include <cmft/cubemapfilter.h>
	#if defined(_CRTDBG_MAP_ALLOC)
		#define malloc(s) _malloc_dbg(s, _NORMAL_BLOCK, __FILE__, __LINE__)
		#define realloc(p, s) _realloc_dbg(p, s, _NORMAL_BLOCK, __FILE__, __LINE__)
		#define free(p) _free_dbg(p, _NORMAL_BLOCK)
	#endif

	// openexr - HDR .exr reads (OpenEXRCore C API)
	#pragma warning(push)
	#pragma warning(disable : 4201) // nonstandard extension: nameless struct/union
	#define OPENEXR_EXPORT
	#include "openexr/src/lib/OpenEXRCore/openexr.h"
	#pragma warning(pop)

	// stb - image load / resize / write (consumption only; implementation in the Prebuilts unity .cpp)
	#include "stb/stb_image.h"
	#include "stb/stb_image_resize2.h"
	#include "stb/stb_image_write.h"

	// tinygltf - glTF scene loader (consumption only; implementation in the Prebuilts unity .cpp). Its
	// vendored nlohmann::json is included above via the shared BT_ENGINE || BT_DATA_PACKER gate.
	#include "tinygltf/tiny_gltf.h"

	// meshoptimizer - vertex remap / cache optimization
	#include "meshoptimizer.h"

	// SPIRV-Cross - shader reflection. The CRT debug-heap free macro breaks its parse; undef around the
	// include and restore after.
	#if defined(_CRTDBG_MAP_ALLOC)
		#undef free
	#endif
	#include "SPIRV-Cross/spirv_cross.hpp"
	#if defined(_CRTDBG_MAP_ALLOC)
		#define free(p) _free_dbg(p, _NORMAL_BLOCK)
	#endif
#endif

// Re-enable warnings after external headers
#ifdef __clang__
	#pragma clang diagnostic pop
#endif
#pragma warning(pop)

// Sanity check to make sure DEBUG/NDEBUG/_DEBUG/_NDEBUG are defined correctly
#if defined(BT_DEBUG) && (!defined(DEBUG) || !defined(_DEBUG) || defined(NDEBUG) || defined(_NDEBUG))
	#error "BT_DEBUG vs DEBUG/_DEBUG/NDEBUG/_NDEBUG inconsistent"
#elif !defined(BT_DEBUG) && (defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG) || !defined(_NDEBUG))
	#error "BT_DEBUG vs DEBUG/_DEBUG/NDEBUG/_NDEBUG inconsistent"
#endif

#undef ASSERT
#undef assert
#define assert USE_ASSERT_NOT_assert

// Disable specific warnings
#pragma warning(disable : 4324) // Structure was padded due to alignment specifier

// Disable specific code analysis warnings
#pragma warning(disable : 26429) // Symbol is never tested for nullness, it can be marked as not_null (DT: Requires using gsl::not_null)
#pragma warning(disable : 26432) // If you define or delete any default operation in the type, define or delete them all (c.21). (DT: Makes class declarations way too messy)
#pragma warning(disable : 26440) // Function can be declared 'noexcept' (DT: Makes code messy with noexcept everywhere)
#pragma warning(disable : 26446) // Prefer to use gsl::at() instead of unchecked subscript operator (DT: Requires gsl::at(), [] is totally fine)
#pragma warning(disable : 26451) // Using operator on a 4 byte value and then casting the result to a 8 byte value (DT: This is way overkill, unlikely to ever have overflow like this)
#pragma warning(disable : 26455) // Default constructor may not throw. Declare it 'noexcept' (f.6). (DT: It's totally fine that constructors throw, https://github.com/isocpp/CppCoreGuidelines/issues/231)
#pragma warning(disable : 26460) // The reference argument can be marked as const (DT: Function signatures are consistent patterns across the codebase)
#pragma warning(disable : 26462) // The value pointed to by is assigned only once, mark it as a pointer to const (DT: const everywhere is messy)
#pragma warning(disable : 26472) // Don't use a static_cast for arithmetic conversions. Use brace initialization, gsl::narrow_cast or gsl::narrow (DT: Requires gsl::narrow)
#pragma warning(disable : 26481) // Don't use pointer arithmetic. Use span instead (bounds.1). (DT: span is too slow in debug)
#pragma warning(disable : 26482) // Only index into arrays using constant expressions (bounds.2). (DT: .at() is too slow in debug)
#pragma warning(disable : 26485) // No array to pointer decay (DT: I need to pass things into APIs...)
#pragma warning(disable : 26490) // Don't use reinterpret_cast (DT: I need this for reading from files and for workbuffers)
#pragma warning(disable : 26496) // The variable is assigned only once, mark it as const (DT: const everywhere is messy)
#pragma warning(disable : 26812) // The enum type is unscoped. Prefer 'enum class' over 'enum' (Enum.3). (DT: Can't selectively disable this, Vulkan enums are slipping through)
#pragma warning(disable : 26821) // For '', consider using gsl::span instead of std::span to guarantee runtime bounds safety (gsl.view). (DT: I'm not using gsl classes)
#pragma warning(disable : 26414) // Move, copy, reassign or reset a local smart pointer (r.5). (DT: Manager singletons in Main.cpp are created once and live for the program lifetime, intentionally simple ownership pattern)
#pragma warning(disable : 26426) // Global initializer calls a non-constexpr function (i.22). (DT: Wrapper class globals hold std::vector for discrete enums, cannot be constexpr by design)
#pragma warning(disable : 26447) // The function is declared 'noexcept' but calls function which may throw exceptions (f.6). (DT: Pairs with disabled 26440, destructors/cleanup functions calling internal engine code won't throw in practice)
#pragma warning(disable : 26467) // Converting from floating point to unsigned integral types results in non-portable code if the double/float has a negative value (es.46). (DT: Would require gsl::narrow_cast or gsl::narrow, project doesn't use GSL)
#pragma warning(disable : 26476) // Expression/symbol uses a naked union with multiple type pointers: Use variant instead (type.7). (DT: Windows VARIANT/PROPVARIANT types for COM interop cannot use std::variant)
#pragma warning(disable : 26813) // Use 'bitwise and' to check if a flag is set. (DT: False positives when intentionally checking exact enum equality, not flag membership)
#pragma warning(disable : 26427) // Global initializer accesses extern object (i.22). (DT: TweaksScreen slider arrays bind to extern Wrapper globals by design)
#pragma warning(disable : 26461) // Pointer argument can be marked as pointer to const (con.3). (DT: Vulkan handle typedefs are pointers to opaque types; const-qualifying handles is non-idiomatic)
#pragma warning(disable : 26814) // The const variable can be computed at compile-time. Consider using constexpr (con.5). (DT: clang-cl rejects offsetof in constexpr contexts; extern-const definitions cannot be inline constexpr without header relocation)

// Portable constexpr offsetof for layout-lock static_asserts. UCRT's offsetof macro expands to a
// reinterpret_cast that MSVC's cl.exe tolerates inside a constant expression (non-conforming extension)
// but clang's frontend (clang-tidy / clang-cl) rejects as a hard error. clang/GCC accept the
// __builtin_offsetof intrinsic in a constant expression, so route through it there; fall back to the
// standard offsetof under MSVC.
#if defined(__clang__) || defined(__GNUC__)
	#define BT_OFFSETOF(type, member) __builtin_offsetof(type, member)
#else
	#define BT_OFFSETOF(type, member) offsetof(type, member)
#endif
