// Disable specific warnings
#pragma warning(disable : 4324) // Structure was padded due to alignment specifier

// Disable specific code analysis warnings
#pragma warning(disable : 26429) // Symbol is never tested for nullness, it can be marked as not_null (DT: Requires using gsl::not_null)
#pragma warning(disable : 26432) // If you define or delete any default operation in the type, define or delete them all (c.21). (DT: Makes class declarations way too messy)
#pragma warning(disable : 26434) // Function '' hides a non - virtual function (DT: I want to hide base functions in ex: ObjectControllerPool but I can't make them virtual !is_trivially_copyable)
#pragma warning(disable : 26440) // Function can be declared 'noexcept' (DT: Makes code messy with noexcept everywhere)
#pragma warning(disable : 26446) // Prefer to use gsl::at() instead of unchecked subscript operator (DT: Requires gsl::at(), [] is totally fine)
#pragma warning(disable : 26451) // Using operator on a 4 byte value and then casting the result to a 8 byte value (DT: This is way overkill, unlikely to ever have overflow like this)
#pragma warning(disable : 26455) // Default constructor may not throw. Declare it 'noexcept' (f.6). (DT: It's totally fine that constructors throw, https://github.com/isocpp/CppCoreGuidelines/issues/231)
#pragma warning(disable : 26462) // The value pointed to by is assigned only once, mark it as a pointer to const (DT: const everywhere is messy)
#pragma warning(disable : 26472) // Don't use a static_cast for arithmetic conversions. Use brace initialization, gsl::narrow_cast or gsl::narrow (DT: Requires gsl::narrow)
#pragma warning(disable : 26481) // Don't use pointer arithmetic. Use span instead (bounds.1). (DT: span is too slow in debug)
#pragma warning(disable : 26482) // Only index into arrays using constant expressions (bounds.2). (DT: .at() is too slow in debug)
#pragma warning(disable : 26485) // No array to pointer decay (DT: I need to pass things into APIs...)
#pragma warning(disable : 26490) // Don't use reinterpret_cast (DT: I need this for reading from files and for workbuffers)
#pragma warning(disable : 26496) // The variable is assigned only once, mark it as const (DT: const everywhere is messy)
#pragma warning(disable : 26812) // The enum type is unscoped. Prefer 'enum class' over 'enum' (Enum.3). (DT: Can't selectively disable this, Vulkan enums are slipping through)
#pragma warning(disable : 26821) // For '', consider using gsl::span instead of std::span to guarantee runtime bounds safety (gsl.view). (DT: I'm not using gsl classes)

#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a ## b

#if defined(BT_DEBUG)
	#define DEBUG_BREAK() do { static bool sbBreak = true; if (sbBreak && IsDebuggerPresent() == TRUE) [[unlikely]] { __debugbreak(); sbBreak = false; } } while(0)
#else
	#define DEBUG_BREAK() ((void)0)
#endif

#if defined(ENABLE_LOGGING)
	#define LOG(a, ...) common::Log(a, __VA_ARGS__)
	#define LOG_INDENT(a) common::LogIndent(a)
	#define SCOPED_LOG_INDENT() common::LogIndent(1); common::ScopedLambda CONCAT(scopedLogIndent, __LINE__)([](){ common::LogIndent(-1); })
#else
	#define LOG(a, ...) ((void)0)
	#define LOG_INDENT(a) ((void)0)
	#define SCOPED_LOG_INDENT() ((void)0)
#endif

class DeviceLostException : public std::exception
{
public:

	DeviceLostException(const char* pcWhat)
	: std::exception(pcWhat)
	{
	}
};

#undef ASSERT
#define ASSERT(a) do { if (!(a)) [[unlikely]] { common::Log("ASSERT \"" #a "\""); DEBUG_BREAK(); throw std::exception("ASSERT: " #a); } } while (0)
#define CHECK_HRESULT(a) do { HRESULT checkHresult = a; if (checkHresult < 0) [[unlikely]] { common::Log("CHECK_HRESULT \""  #a "\" {} 0x{:X}: {}", checkHresult, static_cast<uint32_t>(checkHresult), common::HresultToString(checkHresult).data()); DEBUG_BREAK(); throw std::exception("CHECK_HRESULT: " #a); } } while (0)
#define VERIFY_SUCCESS(a) do { if (!(a)) [[unlikely]] { common::Log("VERIFY_SUCCESS \"" #a "\": {}", common::LastErrorString().data()); DEBUG_BREAK(); throw std::exception("VERIFY_SUCCESS: " #a); } } while (0)
#define CHECK_VK(a) \
do \
{ \
	VkResult checkVkResult = a; \
	if (checkVkResult == VK_SUCCESS) [[likely]] \
	{ \
		break; \
	} \
	 \
	const char* pcResult = VkResultToChar(checkVkResult); \
	common::Log("CHECK_VK \""  #a "\": {}", pcResult); \
	 \
	static char spcException[1024] {}; \
	strcpy_s(spcException, std::size(spcException) - 1, "CHECK_VK failed: " #a "\nVkResult: "); \
	strcat_s(spcException, std::size(spcException) - 1, pcResult); \
	 \
	if (checkVkResult == VK_ERROR_OUT_OF_DATE_KHR || checkVkResult == VK_SUBOPTIMAL_KHR) \
	{ \
		gpGraphics->meDestroyType = DestroyType::kSwapchain; \
		break; \
	} \
	else if (checkVkResult == VK_ERROR_SURFACE_LOST_KHR) \
	{ \
		gpGraphics->meDestroyType = DestroyType::kSurface; \
		break; \
	} \
	else if (checkVkResult == VK_ERROR_DEVICE_LOST) \
	{ \
		throw DeviceLostException(spcException); \
		break; \
	} \
	 \
	DEBUG_BREAK(); \
	throw std::exception(spcException); \
} \
while (0)

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	#define VK_NAME(a, b, c) \
	{ \
		if (gpInstanceManager->mVkSetDebugUtilsObjectNameEXT != nullptr) \
		{ \
			VkDebugUtilsObjectNameInfoEXT vkDebugUtilsObjectNameInfoEXT = \
			{ \
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, \
				.pNext = nullptr, \
				.objectType = a, \
				.objectHandle = reinterpret_cast<uint64_t>(b), \
				.pObjectName = c, \
			}; \
			gpInstanceManager->mVkSetDebugUtilsObjectNameEXT(gpDeviceManager->mVkDevice, &vkDebugUtilsObjectNameInfoEXT); \
		} \
	 }
#else
	#define VK_NAME(a, b, c) ((void)0)
#endif

#if defined(ENABLE_NAVMESH_DISPLAY)
	#define VERIFY_SIZE(a, b) if constexpr (sizeof(a) != b) { }
#else
	#define VERIFY_SIZE(a, b) if constexpr (sizeof(a) != b) { LOG("Change VERIFY_SIZE value to: {}", sizeof(a)); DEBUG_BREAK(); }
#endif

#if defined(ENABLE_PROFILING)
	#define PROFILE_MANAGER_RESET_GLOBAL_QUERY_POOLS(a, b) engine::gpProfileManager->ResetGlobalQueryPools(a, b)
	#define PROFILE_MANAGER_RESET_MAIN_QUERY_POOLS(a, b) engine::gpProfileManager->ResetMainQueryPools(a, b)
	#define PROFILE_MANAGER_RESET_IMAGE_QUERY_POOLS(a, b) engine::gpProfileManager->ResetImageQueryPools(a, b)
	#define PROFILE_TOGGLE_TEXT() engine::gpProfileManager->ToggleProfileText()
	#define BOOT_TIMER_START(a) engine::gpProfileManager->BootStart(a)
	#define BOOT_TIMER_STOP(a) engine::gpProfileManager->BootStop(a)
	#define SCOPED_BOOT_TIMER(a) BOOT_TIMER_START(a); common::ScopedLambda CONCAT(scopedBootTimer, __LINE__)([=](){ BOOT_TIMER_STOP(a); })
	#define BOOT_TIMERS_LOG() engine::gpProfileManager->BootLog()
	#define PROFILE_SET_COUNT(a, b) engine::gpCpuCounters[a].iCount = b
	#define CPU_PROFILE_START(a) engine::gpProfileManager->CpuStart(a, 1)
	#define CPU_PROFILE_START_MULTITHREADED(a, b) engine::gpProfileManager->CpuStart(a, b)
	#define SCOPED_CPU_PROFILE(a) CPU_PROFILE_START(a); common::ScopedLambda CONCAT(scopedCpuProfile, __LINE__)([=](){ CPU_PROFILE_STOP(a); })
	#define SCOPED_CPU_PROFILE_MULTITHREADED(a, b) CPU_PROFILE_START_MULTITHREADED(a, b); common::ScopedLambda CONCAT(scopedCpuProfile, __LINE__)([=](){ CPU_PROFILE_STOP(a); })
	#define CPU_PROFILE_STOP(a) engine::gpProfileManager->CpuStop(a, false)
	#define CPU_PROFILE_STOP_AND_SMOOTH(a) engine::gpProfileManager->CpuStop(a, true)
	#define GPU_PROFILE_START(a, b, c) engine::gpProfileManager->GpuStart(a, b, c)
	#define GPU_PROFILE_STOP(a, b, c) engine::gpProfileManager->GpuStop(a, b, c)
	#define GPU_PROFILE_READ(a, b, c) engine::gpProfileManager->GpuRead(a, b, c)
	#define UPDATE_PROFILE_TEXT() engine::gpProfileManager->UpdateProfileText()
#else
	#define PROFILE_MANAGER_RESET_GLOBAL_QUERY_POOLS(a, b) ((void)0)
	#define PROFILE_MANAGER_RESET_MAIN_QUERY_POOLS(a, b) ((void)0)
	#define PROFILE_MANAGER_RESET_IMAGE_QUERY_POOLS(a, b) ((void)0)
	#define PROFILE_TOGGLE_TEXT() ((void)0)
	#define BOOT_TIMER_START(a) ((void)0)
	#define BOOT_TIMER_STOP(a) ((void)0)
	#define SCOPED_BOOT_TIMER(a) ((void)0)
	#define BOOT_TIMERS_LOG() ((void)0)
	#define PROFILE_SET_COUNT(a, b) ((void)0)
	#define CPU_PROFILE_START(a) ((void)0)
	#define CPU_PROFILE_START_MULTITHREADED(a, b) ((void)0)
	#define SCOPED_CPU_PROFILE(a) ((void)0)
	#define SCOPED_CPU_PROFILE_MULTITHREADED(a, b) ((void)0)
	#define CPU_PROFILE_STOP(a) ((void)0)
	#define CPU_PROFILE_STOP_AND_SMOOTH(a) ((void)0)
	#define GPU_PROFILE_START(a, b, c) ((void)0)
	#define GPU_PROFILE_STOP(a, b, c) ((void)0)
	#define GPU_PROFILE_READ(a, b, c) ((void)0)
	#define UPDATE_PROFILE_TEXT() ((void)0)
#endif
