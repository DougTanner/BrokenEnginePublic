#pragma once

#include "Frame/Alignments.h"

namespace engine
{

// Zone grid dimensions and pre-allocation size
inline constexpr int32_t kiCollisionZonesX = 8;
inline constexpr int32_t kiCollisionZonesY = 8;
inline constexpr int64_t kiCollisionZonePreallocate = 1024;
inline constexpr int64_t kiCollisionLayerPreallocate = 16;
inline constexpr int64_t kiCollisionLayerPairPreallocate = 16;
inline constexpr int64_t kiCollisionCandidatePreallocate = 512;
inline constexpr int64_t kiCollisionResultPreallocate = 1024;
inline constexpr int64_t kiCollisionResultSpanPreallocate = 1024;

// Collision Flags - Behavior modifiers
enum class CollisionFlags : uint8_t
{
	kDestroyOnCollide = 0x01,
	kAlreadyCollided = 0x02,
};
using CollisionFlags_t = common::Flags<CollisionFlags>;

// Per-layer binding data (provided by collections each frame)
struct CollisionLayer
{
	// Pointers to collection-owned ephemeral static buffers
	const XMVECTOR* pVecStartPositions = nullptr;
	const XMVECTOR* pVecEndPositions = nullptr;
	const float* pfStartTimes = nullptr;       // Normalized absolute tick time
	const float* pfEndTimes = nullptr;         // Normalized absolute tick time
	const float* pfMaxTimes = nullptr;         // Optional exclusive entity-collision cutoff
	const float* pfRadii = nullptr;           // Per-object radii
	const float* pfDamages = nullptr;         // Per-object damages
	CollisionFlags_t* pFlags = nullptr;       // Per-object flags (read/write for kAlreadyCollided)
	const XMVECTOR* pVecVelocities = nullptr; // Optional: velocity/direction per object
	int64_t iCount = 0;
	bool bSweptTest = false;                  // Sweep every pair involving this layer

	// Per-layer constants
	uint16_t uiCategory = 0;
	uint16_t uiCollidesWith = 0;

	// Alignment filtering
	const alignment_t* pAlignments = nullptr;
};

// Collision result (by layer index)
struct CollisionResult
{
	int64_t iOtherIndex = 0;                  // Index in the other layer
	size_t uiOtherLayerIndex = 0;             // Which layer (index into sLayers)
	uint16_t uiOtherCategory = 0;
	float fDamageReceived = 0.0f;
	float fTimeOfImpact = 0.0f;
	XMVECTOR vecContactPoint {};
	XMVECTOR vecSelfPosition {};
	XMVECTOR vecOtherVelocity {};  // Velocity of the colliding object (zero if not provided)
};

// Result span for a single object (offset into sResultEntries + count)
struct CollisionResultSpan
{
	int64_t iOffset = -1;  // -1 means no collision
	int64_t iCount = 0;
};

// Implementation-detail types defined in Collision.cpp
struct ZoneRange;
struct LayerPairZones;
struct CollisionCandidate;

class Collision
{
public:

	// Per-frame layer registration and binding (called in PreCollision phase)
	static size_t AddLayer(const CollisionLayer& rLayer);

	// Collision detection (called by Frame, not collections)
	// Uses the collision groups matrix from the frame to filter group collisions
	static void Collide(const Alignments& rAlignments, FXMVECTOR vecArea);

	// Query by layer + index
	static bool HasCollision(size_t uiLayerIndex, int64_t iIndex);
	static std::span<const CollisionResult> GetCollisions(size_t uiLayerIndex, int64_t iIndex);

	// Clear layers for next frame (called at end of PostCollision phase)
	static void Clear();

private:

	static void SetupZones(FXMVECTOR vecArea);
	static ZoneRange CalculateZoneRange(float fMinX, float fMaxX, float fMinY, float fMaxY, float fRadius);
	static ZoneRange CalculateObjectZoneRange(const CollisionLayer& rLayer, int64_t iIndex, bool bSweptPair);
	static void InsertObjectIntoZones(LayerPairZones& rPairZones, int64_t iIndex, const ZoneRange& rRange, bool bIsLayerA);
	static void InsertLayerObjectsIntoZones(LayerPairZones& rPairZones, const CollisionLayer& rLayer, bool bIsLayerA, bool bSweptPair);
	static void CollideLayerPair(const Alignments& rAlignments, LayerPairZones& rPairZones);
	static void CommitCandidate(const CollisionCandidate& rCandidate);
	static void AllocateResultStorage();

	// thread_local: each Dispatch worker and reconcile thread gets its own copy
	static thread_local float sfAreaMinX;
	static thread_local float sfAreaMinY;
	static thread_local float sfZoneWidth;
	static thread_local float sfZoneHeight;

	static thread_local std::vector<CollisionLayer> sLayers;
	static thread_local int64_t siLayerCount;

	static thread_local std::vector<LayerPairZones> sLayerPairZones;
	static thread_local int64_t siLayerPairCount;

	static thread_local std::vector<CollisionResult> sResultEntries;
	static thread_local std::vector<CollisionResultSpan> sResultSpans;
	static thread_local int64_t siResultSpanCount;
	static thread_local int64_t sLayerBaseOffsets[kiCollisionLayerPreallocate];
	static thread_local std::vector<uint32_t> sTestedBGeneration;
	static thread_local uint32_t suiTestedBCurrentGeneration;
};

} // namespace engine
