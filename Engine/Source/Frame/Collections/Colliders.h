#pragma once

#include "Frame/Collections/Collections.h"
#include "Frame/HealthDamage.h"

namespace engine
{

struct CollidersInterpolate : public Collection<CollidersInterpolate, CollectionFlags::kIdToIndex>
{
	static constexpr int64_t kiVersion = 2;
	static constexpr char kpcName[] = "Colliders";

	// SOA members
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfRadii = nullptr;
	uint16_t* __restrict puiCategories = nullptr;
	uint16_t* __restrict puiCollidesWith = nullptr;
	uint8_t* __restrict puiFlags = nullptr;
	float* __restrict pfDamages = nullptr;

	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pfRadii, rSelf.puiCategories, rSelf.puiCollidesWith, rSelf.puiFlags, rSelf.pfDamages); }

	// Interpolate
	static void Update(CollidersInterpolate& __restrict rCurrent, const CollidersInterpolate& __restrict rPrevious);

	// Utility
	bool operator==(const CollidersInterpolate& rOther) const;
};
using collider_t = CollidersInterpolate::id_t;

// Collision result - ephemeral, not serialized
struct CollisionResult
{
	collider_t otherId;
	uint16_t uiOtherCategory;
	float fDamageReceived;
	XMVECTOR vecContactPoint;
};

struct CollidersPostRender : public Collection<CollidersPostRender>
{
	static constexpr int64_t kiVersion = 1;

	// SOA members - IDs for Remove() operations
	collider_t* __restrict puiIds = nullptr;

	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Registration
	static collider_t Add(CollidersInterpolate& rInterpolate, CollidersPostRender& rPostRender, FramePostRenderBase& rFramePostRender,
	                      FXMVECTOR vecPosition, float fRadius, uint16_t uiCategory, uint16_t uiCollidesWith, uint8_t uiFlags, float fDamage);
	static void Remove(CollidersInterpolate& rInterpolate, CollidersPostRender& rPostRender, collider_t id);

	// Position sync (called by source collections in their Update)
	static void UpdatePosition(CollidersInterpolate& rInterpolate, collider_t id, FXMVECTOR vecPosition);

	// Collision detection
	static void Collide(CollidersInterpolate& rInterpolate, CollidersPostRender& rPostRender);

	// Query results (valid only during Collide phase)
	static bool HasCollision(collider_t id);
	static const std::vector<CollisionResult>* GetCollisions(collider_t id);

	// Update
	static void Update(CollidersPostRender& __restrict rCurrent, const CollidersPostRender& __restrict rPrevious);

	// Utility
	bool operator==(const CollidersPostRender& rOther) const;

	// Static storage for collision results - cleared each frame
	static inline std::unordered_map<collider_t, std::vector<CollisionResult>> sCollisionResults;
};

} // namespace engine
