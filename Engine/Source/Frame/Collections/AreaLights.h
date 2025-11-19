#pragma once

#include "Frame/Collections/Collections.h"

namespace game
{

struct Frame;

} // namespace game

namespace engine
{

struct AreaLightsInterpolate
{
	static constexpr int64_t kiVersion = 1;

	AreaLightsInterpolate() = default;
	virtual ~AreaLightsInterpolate() = default;

	int64_t iCount = 0;
	int64_t iCapacity = 0;

	common::AlignedUniquePtr<std::byte> pData;
	// 1. IMPORTANT: Add to this macro when adding new members
	#define AREA_LIGHTS_INTERPOLATE_LIST(a) a.pVecPositions
	XMVECTOR* __restrict pVecPositions = nullptr;

	inline bool operator==(const AreaLightsInterpolate& rOther) const
	{
		bool bEqual = true;
		bEqual &= common::BreakOnNotEqual(iCount, rOther.iCount);
		bEqual &= common::BreakOnNotEqual(iCapacity, rOther.iCapacity);

		for (int64_t i = 0; i < iCount; ++i)
		{
			// 2. IMPORTANT: Add a BreakOnNotEqual when adding members
			bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		}

		return bEqual;
	}

	static inline area_light_t Add()
	{
		return 0;
	}

	static inline common::crc_t Checksum(const AreaLightsInterpolate& rCurrent)
	{
		common::crc_t checksum = 0;
		checksum ^= common::Crc(rCurrent.iCount);
		checksum ^= common::Crc(rCurrent.iCapacity);
		checksum ^= engine::MultiCrc(rCurrent.iCount, AREA_LIGHTS_INTERPOLATE_LIST(rCurrent));
		return checksum;
	}
};

inline std::ostream& operator<<(std::ostream& rStream, const AreaLightsInterpolate& rCurrent)
{
	common::Write(rStream, rCurrent.iCount);
	common::Write(rStream, rCurrent.iCapacity);
	engine::MultiWrite(rStream, rCurrent.iCount, AREA_LIGHTS_INTERPOLATE_LIST(rCurrent));
	return rStream;
}

inline std::istream& operator>>(std::istream& rStream, AreaLightsInterpolate& rCurrent)
{
	common::Read(rStream, rCurrent.iCount);
	common::Read(rStream, rCurrent.iCapacity);
	engine::AllocateAndRead(rCurrent, rStream, AREA_LIGHTS_INTERPOLATE_LIST(rCurrent));
	return rStream;
}

struct AreaLightsInterpolate
{
	static constexpr int64_t kiVersion = 1;

	static void Spawn(Frame& __restrict rFrame);
}

} // namespace engine
