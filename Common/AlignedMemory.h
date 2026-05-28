#pragma once

namespace common
{

// Custom deleter for aligned memory allocated with _aligned_malloc
// Automatically calls _aligned_free when std::unique_ptr is destroyed
// Used with std::unique_ptr to provide RAII for aligned memory allocations
struct AlignedDeleter
{
	void operator()(void* p) const noexcept
	{
		_aligned_free(p);
	}
};

// Type alias for std::unique_ptr with aligned memory management
// Provides RAII semantics for 64-byte aligned allocations required for SIMD operations
// Template parameter: T - Element type for the array
template<typename T>
using AlignedUniquePtr = std::unique_ptr<T[], AlignedDeleter>;

// Factory function for creating aligned memory with custom alignment
// Allocates memory aligned to specified boundary using _aligned_malloc
// Throws std::bad_alloc if allocation fails
// Parameters: uiCount - Number of elements to allocate, iAlignment - Alignment boundary in bytes
// Returns: AlignedUniquePtr managing the allocated memory
template<typename T>
AlignedUniquePtr<T> MakeAligned(int64_t uiCount)
{
	return AlignedUniquePtr<T>(static_cast<T*>(_aligned_malloc(uiCount * sizeof(T), 64)));
}

} // namespace common
