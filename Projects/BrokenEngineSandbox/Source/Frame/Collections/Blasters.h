#pragma once

#include "Frame/Collision.h"
#include "Frame/Collections/AreaLights.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Sounds.h"

namespace game
{

struct BlastersInterpolate : public engine::Collection<BlastersInterpolate>
{
	// Types
	struct Type
	{
		XMFLOAT2 f2Size {0.11f, 1.5f};
		uint8_t uiAreaLightTypeIndex = 0;
	};

	static inline std::vector<Type> sTypes;

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame, float fDeltaTime);

	uint8_t* __restrict puiTypeIndices = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	engine::area_lights_t* __restrict puiAreaLights = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiTypeIndices, rSelf.pVecPositions, rSelf.puiAreaLights); }

	// Utility
	bool operator==(const BlastersInterpolate& rOther) const;
};

enum class BlasterFlags : uint8_t
{
	kDestroy       = 0x01,
	kCollidePlayer = 0x02,
};
using BlasterFlags_t = common::Flags<BlasterFlags>;

struct BlastersPostRender : public engine::Collection<BlastersPostRender>
{
	// Update
	static void Update(BlastersPostRender& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PreCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PostCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void XM_CALLCONV Spawn(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, uint8_t uiTypeIndex, BlasterFlags_t flags = {});
	static void Destroy(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	BlasterFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	engine::sound_t* __restrict puiSounds = nullptr;
	float* __restrict pfPitches = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.puiSounds, rSelf.pfPitches); }

	// Utility
	bool operator==(const BlastersPostRender& rOther) const;
};

} // namespace game

#if 0

#include "Frame/Collections/Collection.h"
#include "Frame/Pools/Lighting.h"
#include "Frame/Pools/Sounds.h"

namespace game
{

enum class BlasterFlags : uint8_t
{
	kDestroy       = 0x01,
	kSizeFromSpeed = 0x02,

	kCollideEnemies = 0x04,
	kCollidePlayer  = 0x08,

	kImpactObject  = 0x10,
	kImpactTerrain = 0x20,
};
using BlasterFlags_t = common::Flags<BlasterFlags>;

inline constexpr int64_t kiMaxSpawnBlasters = 512;

struct SpawnBlaster
{
	BlasterFlags_t flags;
	common::crc_t crc = 0;
	XMVECTOR vecPosition {};
	XMVECTOR vecVelocity {};
	XMFLOAT2 f2Size {};
	float fVisibleIntensity = 0.0f;
	float fLightArea = 0.0f;
	float fLightIntensity = 0.0f;
	float fDamage = 0.0f;
	XMFLOAT4 f4Decays{};

	inline bool operator==(const SpawnBlaster& rOther) const = default;
};

// Spawnable requires 1. operator== 2. Main() 3. Spawn()
struct alignas(64) Blasters : public engine::Spawnable<SpawnBlaster, kiMaxSpawnBlasters>
{
	static constexpr int64_t kiMax = 2048;

	// Interpolate
	int64_t iCount = 0;

	alignas(64) BlasterFlags_t pFlags[kiMax] {};
	alignas(64) float pfTimes[kiMax] {};
	alignas(64) XMVECTOR pVecPositions[kiMax] {};
	alignas(64) XMVECTOR pVecVelocities[kiMax] {};
	alignas(64) engine::area_light_t puiAreaLights[kiMax] {};
	alignas(64) common::crc_t pCrcs[kiMax] {};
	alignas(64) XMFLOAT2 pf2Sizes[kiMax] {};
	alignas(64) float pfFreezeTimes[kiMax] {};
	alignas(64) float pfVisibleIntensities[kiMax] {};
	alignas(64) float pfLightAreas[kiMax] {};
	alignas(64) float pfLightIntensities[kiMax] {};

	// Post render
	alignas(64) float pfSlowTimes[kiMax] {};
	alignas(64) float pfDamages[kiMax] {};
	alignas(64) float pfPitches[kiMax] {};
	alignas(64) engine::sound_t puiSounds[kiMax] {};
	alignas(64) XMFLOAT4 pf4Decays[kiMax] {}; // Velocity, Visible, Size, Damage

	// Utility
	inline bool operator==(const Blasters& rOther) const;
	void Copy(int64_t iDestIndex, int64_t iSrcIndex);

	// Update
	static void Interpolate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void PostRender(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void XM_CALLCONV CollisionEffect(Frame& __restrict rFrame, int64_t i, bool bSmoke = true);
	static void Collide(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Spawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Destroy(Frame& __restrict rFrame, int64_t i);
	static void Destroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);

	// Render
	static void RenderMain(int64_t iCommandBuffer, const Frame& __restrict rFrame);
};
static_assert(std::is_trivially_copyable_v<Blasters>);
inline constexpr int64_t kiBlastersVersion = 9 + sizeof(Blasters);

} // namespace game

#endif
