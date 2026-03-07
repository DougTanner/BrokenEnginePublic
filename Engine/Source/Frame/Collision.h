#pragma once

namespace game
{

struct Frame;

}

namespace engine
{

// Zone grid dimensions and pre-allocation size
inline constexpr int32_t kiCollisionZonesX = 8;
inline constexpr int32_t kiCollisionZonesY = 8;
inline constexpr int64_t kiCollisionZonePreallocate = 256;
inline constexpr int64_t kiCollisionLayerPreallocate = 16;
inline constexpr int64_t kiCollisionLayerPairPreallocate = 16;
inline constexpr int64_t kiAreaDamageSourcePreallocate = 16;

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
	const XMVECTOR* pVecPositions = nullptr;
	const float* pfRadii = nullptr;           // Per-object radii
	const float* pfDamages = nullptr;         // Per-object damages
	CollisionFlags_t* pFlags = nullptr;       // Per-object flags (read/write for kAlreadyCollided)
	const XMVECTOR* pVecVelocities = nullptr; // Optional: velocity/direction per object
	int64_t iCount = 0;
	bool bSweptTest = false;                  // Use swept sphere test (requires pVecVelocities)

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
	XMVECTOR vecContactPoint {};
	XMVECTOR vecOtherVelocity {};  // Velocity of the colliding object (zero if not provided)
};

// Area damage source (registered when objects explode)
struct AreaDamageSource
{
	XMVECTOR vecPosition {};
	float fRadius = 0.0f;
	float fDamage = 0.0f;
	uint16_t uiCategory = 0;
};

// Per-zone storage for a layer pair
struct ZonePair
{
	std::vector<int64_t> indicesA;  // Object indices from layer A
	std::vector<int64_t> indicesB;  // Object indices from layer B
	int64_t iCountA = 0;
	int64_t iCountB = 0;
};

// Grid of zones for one layer pair
struct LayerPairZones
{
	size_t uiLayerA = 0;
	size_t uiLayerB = 0;
	ZonePair zones[kiCollisionZonesY][kiCollisionZonesX];

	LayerPairZones()
	{
		for (ZonePair (&rRow)[kiCollisionZonesX] : zones)
		{
			for (ZonePair& rZonePair : rRow)
			{
				rZonePair.indicesA.resize(kiCollisionZonePreallocate);
				rZonePair.indicesB.resize(kiCollisionZonePreallocate);
			}
		}
	}
};

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
	static const std::vector<CollisionResult>* GetCollisions(size_t uiLayerIndex, int64_t iIndex);

	// Clear layers for next frame (called at end of PostCollision phase)
	static void Clear();

	// Area damage registration (called in PostCollision when objects explode)
	static void AddAreaDamage(const AreaDamageSource& rSource);

	// Query area damage at a position (called in AreaDamage phase)
	// Returns total damage with linear falloff applied, filtered by category mask
	// Outputs the closest damage source position via rvecClosestSource
	static float GetAreaDamage(FXMVECTOR vecPosition, uint16_t uiCategoryMask, XMVECTOR& rvecClosestSource);

	// Clear area damage sources (called at end of AreaDamage phase)
	static void ClearAreaDamage();

private:

	static void SetupZones(FXMVECTOR vecArea);
	static void InsertIntoZones(LayerPairZones& rPairZones, int64_t iIndex, FXMVECTOR vecPosition, float fRadius, bool bIsLayerA);
	static void InsertIntoZonesSwept(LayerPairZones& rPairZones, int64_t iIndex, FXMVECTOR vecMin, FXMVECTOR vecMax, float fRadius, bool bIsLayerA);
	static void CollideLayerPair(const Alignments& rAlignments, LayerPairZones& rPairZones);

	// thread_local: each Dispatch worker and reconcile thread gets its own copy
	static thread_local float sfAreaMinX;
	static thread_local float sfAreaMinY;
	static thread_local float sfZoneWidth;
	static thread_local float sfZoneHeight;

	static thread_local std::vector<CollisionLayer> sLayers;
	static thread_local int64_t siLayerCount;

	static thread_local std::vector<AreaDamageSource> sAreaDamageSources;
	static thread_local int64_t siAreaDamageSourceCount;

	static thread_local std::vector<LayerPairZones> sLayerPairZones;
	static thread_local int64_t siLayerPairCount;

	static thread_local std::unordered_map<uint64_t, std::vector<CollisionResult>> sResults;
};

} // namespace engine
