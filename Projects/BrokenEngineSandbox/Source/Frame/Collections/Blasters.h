#pragma once

#include "Frame/Alignments.h"
#ifdef BT_CLIENT
#include "Frame/Collections/AreaLights.h"
#endif
#include "Frame/Collections/Collection.h"
#ifdef BT_CLIENT
#include "Frame/Collections/Sounds.h"
#include "Frame/Collections/WindTrails.h"
#endif

namespace game
{

struct BlastersType
{
	XMFLOAT2 f2Size {0.11f, 1.5f};
	uint8_t uiAreaLightTypeIndex = 0;
};

struct BlastersInterpolate : public engine::Collection<BlastersInterpolate>,
                             public engine::TypeRegistry<BlastersType>
{
	static constexpr char kName[] = "Blasters";
	static constexpr common::crc_t kCrc = common::CrcConsteval(kName);

	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources();

	// Allocate and copy
	static void AllocateAndCopy(BlastersInterpolate& rCurrent, const BlastersInterpolate& rPrevious);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame);

#ifdef BT_CLIENT
	static void AllocateClientObjects(Frame& rFrame, int64_t iIndex);
	static void HydrateClientObjects(Frame& rFrame);
#endif

	// Render
#ifdef BT_CLIENT
	static void BeginRender(int64_t, const std::unordered_map<engine::GridCoord, FrameInterpolate>&, const std::vector<engine::GridCoord>&) {}
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t) {}
#endif

	uint8_t* __restrict puiTypeIndices = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;
#ifdef BT_CLIENT
	engine::area_lights_t* __restrict puiAreaLights = nullptr;
	engine::wind_trail_t* __restrict puiWindTrails = nullptr;
	float* __restrict pfWindTrailIntensities = nullptr;
	float* __restrict pfWindTrailWidths = nullptr;
	float* __restrict pfWindTrailLengthMultipliers = nullptr;
#endif
	auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.puiTypeIndices, rSelf.pVecPositions, rSelf.pVecDirections); }
#ifdef BT_CLIENT
	auto ClientMembers(this auto&& rSelf) { return std::tie(rSelf.puiAreaLights, rSelf.puiWindTrails, rSelf.pfWindTrailIntensities, rSelf.pfWindTrailWidths, rSelf.pfWindTrailLengthMultipliers); }
#endif
	auto Members(this auto&& rSelf)
	{
#ifdef BT_CLIENT
		return std::tuple_cat(rSelf.SharedMembers(), rSelf.ClientMembers());
#else
		return rSelf.SharedMembers();
#endif
	}

	// Utility
	bool operator==(const BlastersInterpolate& rOther) const;
};

enum class BlasterFlags : uint8_t
{
	kDestroy       = 0x01,
	kTransfer      = 0x02,
};
using BlasterFlags_t = common::Flags<BlasterFlags>;

struct BlastersPostRender : public engine::Collection<BlastersPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(BlastersPostRender& rCurrent, const BlastersPostRender& rPrevious);

	// Update
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Transfer(Frame& __restrict rFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame);

	BlasterFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
#ifdef BT_CLIENT
	engine::sound_t* __restrict puiSounds = nullptr;
#endif
	float* __restrict pfPitches = nullptr;
	engine::alignment_t* __restrict pAlignments = nullptr;
	auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.pfPitches, rSelf.pAlignments); }
#ifdef BT_CLIENT
	auto ClientMembers(this auto&& rSelf) { return std::tie(rSelf.puiSounds); }
#endif
	auto Members(this auto&& rSelf)
	{
#ifdef BT_CLIENT
		return std::tuple_cat(rSelf.SharedMembers(), rSelf.ClientMembers());
#else
		return rSelf.SharedMembers();
#endif
	}

	// Utility
	bool operator==(const BlastersPostRender& rOther) const;

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition;
		XMVECTOR vecVelocity;
		uint8_t uiTypeIndex;
		BlasterFlags_t flags {};
		engine::alignment_t alignment {};
		float fWindTrailIntensity = 0.0f;
		float fWindTrailWidth = 0.0f;
		float fWindTrailLengthMultiplier = 1.0f;
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

} // namespace game

namespace engine
{
extern template struct Collection<game::BlastersInterpolate>;
extern template struct Collection<game::BlastersPostRender>;
}
