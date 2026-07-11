#pragma once

#include "Network/NetworkProtocol.h" // kiMaxStatusChangesPerCell for the shared batch-size bounds below

// Declare-in-engine / implement-in-game pattern: these batch (de)serializers are declared in the engine
// layer (so engine networking code can call them) but implemented in the game layer, because their wire
// format is game-specific. The game type is forward-declared rather than included so this engine header
// stays free of game dependencies; the game-side .cpp that defines these functions supplies the full type.
namespace game { struct StatusChange; }

namespace engine
{

// Upper bound on a single serialized StatusChange of any type. A game wire fact, but shared here (the codec is
// declared engine::) so the game SerializeGroup per-item ASSERT and the engine Server's compression-scratch sizing
// reference one source. Bump when any StatusChange payload grows past it.
inline constexpr int64_t kiMaxStatusChangeBytesPerItem = 120;

// Worst-case serialized bytes for a full kiMaxStatusChangesPerCell batch: every item at its max size plus a
// conservative 3-byte group header charged per item (over-bounds the real per-type group headers so this stays
// free of the game enum's type count).
inline constexpr int64_t kiMaxSerializedStatusChangeBatchBytes = kiMaxStatusChangesPerCell * (kiMaxStatusChangeBytesPerItem + 3);

// Worst-case bytes CompressStatusChangeBatch can emit for a valid capped batch (4-byte uncompressed-size prefix +
// LZ4 payload). Sizes the server's reused compression scratch so any valid batch always fits; the codec still checks
// the LZ4 return as a belt.
inline constexpr int64_t kiMaxCompressedStatusChangeBatchBytes = static_cast<int64_t>(sizeof(int32_t)) + LZ4_COMPRESSBOUND(kiMaxSerializedStatusChangeBatchBytes);

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
