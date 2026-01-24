#pragma once

#include "Frame/Collections/Collection.h"

namespace game
{

struct Frame;
struct FramePostRender;

}

namespace engine
{

struct FramePostRenderBase;

// Tag type for strong-typed alignment IDs
struct AlignmentTag {};
using alignment_t = id_t<AlignmentTag>;

// Invalid alignment constant (collides with all)
inline constexpr alignment_t kInvalidAlignment {};

// Dynamic alignment matrix
// Manages collision filtering between alignments with runtime add/remove
struct Alignment
{
	int64_t iCount = 0;
	std::vector<uint8_t> matrixData;                          // N*N flat storage (1 = collide, 0 = no)
	std::unordered_map<alignment_t, int64_t> idToIndexMap;    // Stable ID -> index
	std::vector<alignment_t> indexToIdMap;                    // Index -> stable ID

	// Add a new alignment, returns stable ID
	// New alignments default to colliding with all existing alignments
	alignment_t Add(FramePostRenderBase& rFrame);

	// Remove an alignment by ID (compacts matrix and renumbers)
	void Remove(alignment_t id);

	// Set whether two alignments can collide
	void SetCanCollide(alignment_t idA, alignment_t idB, bool bCanCollide);

	// Query whether two alignments can collide
	// Invalid IDs (kInvalidAlignment) return true (collide with all)
	bool CanCollide(alignment_t idA, alignment_t idB) const;

	// Serialization
	bool operator==(const Alignment& rOther) const;
	common::crc_t Crc() const;
	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);

private:

	// Compact matrix after removal, renumbering indices
	void Compact(int64_t iRemovedIndex);
};

} // namespace engine
