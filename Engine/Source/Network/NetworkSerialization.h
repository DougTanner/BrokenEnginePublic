#pragma once

namespace game { struct StatusChange; }

namespace engine
{

// Type-specific serialization (no compression)
// Returns bytes written to pDest
int64_t SerializeStatusChangeBatch(const game::StatusChange* pChanges, int64_t iCount, void* pDest);

// Returns number of StatusChanges written to pDest
int64_t DeserializeStatusChangeBatch(const void* pSource, int64_t iSourceSize, game::StatusChange* pDest, int64_t iMaxCount);

// Serialization + LZ4 compression
// Returns bytes written to pDest (4-byte uncompressed size prefix + compressed data)
int64_t CompressStatusChangeBatch(const game::StatusChange* pChanges, int64_t iCount, void* pDest, int64_t iDestCapacity);

// Returns number of StatusChanges written to pDest
int64_t DecompressStatusChangeBatch(const void* pSource, int64_t iSourceSize, game::StatusChange* pDest, int64_t iMaxCount);

} // namespace engine
