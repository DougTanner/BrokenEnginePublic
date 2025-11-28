#pragma once

#include "Frame/Collections/AreaLights.h"
#include "Frame/Collections/Collections.h"

namespace game
{

struct BlastersInterpolate : public engine::Collection<BlastersInterpolate>
{
	static constexpr int64_t kiVersion = 1;

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

	auto Members(this auto&& self) { return std::tie(self.puiTypeIndices, self.pVecPositions, self.puiAreaLights); }

	// Utility
	bool operator==(const BlastersInterpolate& rOther) const;
};

enum class BlasterFlags : uint8_t
{
	kDestroy = 0x01,
};
using BlasterFlags_t = common::Flags<BlasterFlags>;

struct BlastersPostRender : public engine::Collection<BlastersPostRender>
{
	static constexpr int64_t kiVersion = 1;

	// Update
	static void Update(FramePostRender& __restrict rCurrentFramePostRender, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Collide(Frame& __restrict rFrame);
	static void XM_CALLCONV Spawn(Frame& __restrict rFrame, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, uint8_t uiTypeIndex, BlasterFlags_t flags = {});
	static void Destroy(Frame& __restrict rFrame);

	BlasterFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;

	auto Members() { return std::tie(pFlags, pVecVelocities); }
	auto Members() const { return std::tie(pFlags, pVecVelocities); }

	// Utility
	bool operator==(const BlastersPostRender& rOther) const;
};

} // namespace game

#if 0

#include "Frame/Collections/Collections.h"
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
