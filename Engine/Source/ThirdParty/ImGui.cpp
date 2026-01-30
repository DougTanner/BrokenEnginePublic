#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)

#undef assert
#define assert ASSERT

#include "imgui/imgui.cpp"
#include "imgui/imgui_draw.cpp"
#include "imgui/imgui_tables.cpp"
#include "imgui/imgui_widgets.cpp"
#include "imgui/backends/imgui_impl_win32.cpp"
#include "imgui/backends/imgui_impl_vulkan.cpp"

#pragma warning(pop)
