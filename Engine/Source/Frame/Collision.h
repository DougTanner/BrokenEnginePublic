#pragma once

namespace game
{

struct Frame;

}

namespace engine
{

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
	const float* pfRadii = nullptr;           // Per-object radii (or uniform)
	const float* pfDamages = nullptr;         // Per-object damages (or uniform)
	CollisionFlags_t* pFlags = nullptr;  // Per-object flags (read/write for kAlreadyCollided)
	int64_t iCount = 0;

	// Per-layer constants
	uint16_t uiCategory = 0;
	uint16_t uiCollidesWith = 0;

	// Uniform values (used if per-object arrays are nullptr)
	float fUniformRadius = 0.0f;
	float fUniformDamage = 0.0f;
	CollisionFlags_t uniformFlags {};
};

// Collision result (by layer index)
struct CollisionResult
{
	int64_t iOtherIndex = 0;                  // Index in the other layer
	size_t uiOtherLayerIndex = 0;             // Which layer (index into sLayers)
	uint16_t uiOtherCategory = 0;
	float fDamageReceived = 0.0f;
	XMVECTOR vecContactPoint {};
};

// Area damage source (registered when objects explode)
struct AreaDamageSource
{
	XMVECTOR vecPosition {};
	float fRadius = 0.0f;
	float fDamage = 0.0f;
	uint16_t uiCategory = 0;
};

class Collision
{
public:

	// Per-frame layer registration and binding (called in PreCollision phase)
	static size_t AddLayer(const CollisionLayer& rLayer);

	// Collision detection (called by Frame, not collections)
	static void Collide();

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

	static void CollideLayerPair(size_t uiLayerA, size_t uiLayerB, bool bACollidesWithB, bool bBCollidesWithA);

	static inline std::vector<CollisionLayer> sLayers;
	static inline std::unordered_map<uint64_t, std::vector<CollisionResult>> sResults;
	static inline std::vector<AreaDamageSource> sAreaDamageSources;
};

} // namespace engine
