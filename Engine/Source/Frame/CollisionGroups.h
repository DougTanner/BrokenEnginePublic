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

// Tag type for strong-typed collision group IDs
struct CollisionGroupTag {};
using collision_group_t = id_t<CollisionGroupTag>;

// Invalid group constant (collides with all)
inline constexpr collision_group_t kInvalidCollisionGroup {};

// Dynamic collision group matrix
// Manages collision filtering between groups with runtime add/remove
struct CollisionGroups
{
	int64_t iCount = 0;
	std::vector<uint8_t> matrixData;                              // N*N flat storage (1 = collide, 0 = no)
	std::unordered_map<collision_group_t, int64_t> idToIndexMap;  // Stable ID -> index
	std::vector<collision_group_t> indexToIdMap;                  // Index -> stable ID

	// Add a new collision group, returns stable ID
	// New groups default to colliding with all existing groups
	collision_group_t Add(FramePostRenderBase& rFrame);

	// Remove a collision group by ID (compacts matrix and renumbers)
	void Remove(collision_group_t id);

	// Set whether two groups can collide
	void SetCanCollide(collision_group_t idA, collision_group_t idB, bool bCanCollide);

	// Query whether two groups can collide
	// Invalid IDs (kInvalidCollisionGroup) return true (collide with all)
	bool CanCollide(collision_group_t idA, collision_group_t idB) const;

	// Serialization
	bool operator==(const CollisionGroups& rOther) const;
	common::crc_t Crc() const;
	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);

private:

	// Compact matrix after removal, renumbering indices
	void Compact(int64_t iRemovedIndex);
};

} // namespace engine
