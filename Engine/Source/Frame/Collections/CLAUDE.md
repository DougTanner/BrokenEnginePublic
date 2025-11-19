# `/Engine/Source/Frame/Collections/`

Template utilities for managing dynamically-allocated Structure-of-Arrays collections with efficient memory management and serialization support.

## Architecture Overview

Template helper functions that simplify memory management for dynamically-allocated Structure-of-Arrays collections. These utilities handle common patterns like capacity reallocation, data copying during growth, element removal, and serialization.

## Core Files

### Collections.h

Template utilities for memory management helpers supporting Structure-of-Arrays layout with dynamic allocation.

**Memory Sub-Allocation Helpers**: Template functions for managing multiple arrays within a single contiguous buffer

*Low-Level Helpers*:
- `AssignAligned<T>()` - Aligns pointer to 64-byte boundary before assignment for SIMD optimization
- `AssignAndCopyAligned<T>()` - Aligned variant with data copying for reallocation
- `AllocateAndAssign()` - Generic helper that allocates a single contiguous buffer and assigns multiple member pointers to aligned positions within it

*High-Level Update Pattern Helpers*:
- `ResetDataToNull()` - Handles null data case by resetting buffer and zeroing all member pointers
- `ReallocateIfCapacityChanged()` - Returns bool indicating whether to continue Update(). Handles both null data case (returns false to abort) and capacity changes (reallocates and returns true to continue)
- `GrowCapacityWithCopy()` - Handles Spawn() capacity growth pattern with data copying
- `SwapElement()` - Swaps element at index i with the last element (at rStruct.iCount - 1). Caller is responsible for decrementing count. Provides efficient O(1) unordered element removal pattern used in Destroy() methods.
- `MultiCrc()` - Computes XOR'd CRC checksum of multiple member arrays. Takes count and variadic member pointers, returns combined checksum. Automatically handles zero count case.
- `MultiWrite()` - Serializes multiple member arrays to stream. Takes stream, count, and variadic member pointers. Uses fold expressions to call common::Write for each array.
- `MultiRead()` - Deserializes multiple member arrays from stream. Takes stream, count, and variadic member pointers. Uses fold expressions to call common::Read for each array.
- `AllocateAndRead()` - Combines allocation and deserialization. Takes struct, stream, and variadic member pointers. Calls AllocateAndAssign if capacity > 0, otherwise sets pData to nullptr. Then calls MultiRead to deserialize the data.

**Design Rationale**:

The `AllocateAndAssign()` function simplifies initial dynamic allocation for structures with SOA layout:
- Takes a struct, capacity, and variadic member pointer references
- Automatically calculates buffer size using fold expressions over member pointer types
- Allocates single contiguous buffer with 64-byte alignment
- Positions member pointers to aligned locations within buffer
- Used during deserialization to initialize dynamic arrays with minimal code

The high-level pattern helpers reduce boilerplate in Update(), Spawn(), Destroy(), Checksum(), and stream operators:
- `ResetDataToNull()` eliminates repetitive null checks and pointer zeroing
- `ReallocateIfCapacityChanged()` encapsulates the common pattern of checking for null data and capacity changes. Returns false for null data (signaling Update() to return early), returns true otherwise (allowing Update() to continue). Calculates buffer size internally using fold expressions.
- `GrowCapacityWithCopy()` standardizes the growth pattern (2 * capacity + 1) with data preservation. Calculates buffer size internally using fold expressions.
- `SwapElement()` provides efficient unordered removal in Destroy() methods. Swaps element at index with last element for O(1) removal without preserving order. Caller handles count decrement and index re-checking.
- `MultiCrc()` eliminates repetitive if-count-check and XOR-assignment loops in Checksum() methods. Uses fold expressions to compute combined CRC in a single call.
- `MultiWrite()` and `MultiRead()` eliminate repetitive stream write/read calls for multiple member arrays. Use fold expressions to serialize/deserialize all members in a single call.
- `AllocateAndRead()` combines the allocation pattern (capacity check, AllocateAndAssign or null) with MultiRead into a single call, simplifying stream input operators.

These helpers enable Structure-of-Arrays (SOA) layout within dynamically allocated buffers while maintaining alignment requirements for vectorized operations. Buffer size calculation happens automatically within the helpers, eliminating the need for separate CalculateBufferSize() functions.

## Design Principles

### Memory Efficiency
- Single contiguous allocation per structure with 64-byte alignment for cache efficiency
- RAII cleanup via `common::AlignedUniquePtr<std::byte>`
- Automatic buffer size calculation using fold expressions over member pointer types
- Structure-of-Arrays layout enables efficient vectorized operations

### Determinism
- Helper functions operate deterministically on input data
- Serialization helpers preserve exact state for replay validation
- Checksum computation combines all member arrays via XOR for fast validation

## See Also
- `/Engine/Source/Frame/Pools/` - Object pool management
- `/Projects/*/Source/Frame/Collections/` - Game-specific implementations
- `/Engine/Source/Frame/UpdateList.h` - Update phase definitions