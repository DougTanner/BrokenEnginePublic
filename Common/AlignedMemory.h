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

// Factory function for creating 64-byte SIMD-aligned array storage
// Allocates iCount elements aligned to 64 bytes via _aligned_malloc
// Returns an empty (null) AlignedUniquePtr if _aligned_malloc fails (no throw)
// Parameters: iCount - Number of elements to allocate
// Returns: AlignedUniquePtr managing the allocated memory
template<typename T>
AlignedUniquePtr<T> MakeAligned(int64_t iCount)
{
	static_assert(std::is_trivially_default_constructible_v<T>, "MakeAligned requires a trivially default-constructible type");
	static_assert(std::is_trivially_destructible_v<T>, "MakeAligned requires a trivially destructible type");
	const size_t uiBytes = static_cast<size_t>(iCount) * sizeof(T);
	ASSERT(iCount >= 0 && uiBytes / sizeof(T) == static_cast<size_t>(iCount));
	return AlignedUniquePtr<T>(static_cast<T*>(_aligned_malloc(uiBytes, 64)));
}

} // namespace common
