#include <source_location>
#include <string_view>
namespace common { void Assert(bool bCondition, std::string_view expression, std::source_location loc = std::source_location::current()); }

// ImGui Vulkan backend needs Volk for function loading
#define IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_IMPL_VULKAN_USE_VOLK
#define IMGUI_IMPL_VULKAN_VOLK_FILENAME <volk/volk.h>

#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)

#undef assert
#define assert(a) common::Assert(a, #a)

#include "imgui/imgui.cpp"
#include "imgui/imgui_draw.cpp"
#include "imgui/imgui_tables.cpp"
#include "imgui/imgui_widgets.cpp"
#include "imgui/backends/imgui_impl_win32.cpp"
#include "imgui/backends/imgui_impl_vulkan.cpp"

#include "implot/implot.cpp"
#include "implot/implot_items.cpp"

#pragma warning(pop)
