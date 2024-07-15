// Note: Not using precompiled header so that this file can be optimized in Debug builds
// #pragma optimize( "", off )
#include "Pch.h"

#include "Pushers.h"

#include "Frame/Frame.h"

using namespace DirectX;

namespace engine
{

constexpr float kfArenaSize = 400.0f;
constexpr float kfZoneSize = 8.0f;
constexpr int64_t kiZones = static_cast<int64_t>(common::Ceil(kfArenaSize / kfZoneSize));
constexpr int64_t kiMaxPushersPerZone = 512;

inline alignas(64) uint16_t gppuiPushersPerZone[kiZones][kiZones] {};
inline alignas(64) pusher_t gpppuiPusherZones[kiZones][kiZones][kiMaxPushersPerZone] {};

inline float gfArenaLeft = -0.5f * kfArenaSize;
inline float gfArenaTop = 0.5f * kfArenaSize;

void Pushers::SetupZones(game::Frame& __restrict rFrame)
{
	SCOPED_CPU_PROFILE(kCpuTimerPusherZones);
	PROFILE_SET_COUNT(kCpuCounterPushers, uiMaxIndex);

	ZeroMemory(gppuiPushersPerZone, sizeof(gppuiPushersPerZone));
	gfArenaLeft = XMVectorGetX(rFrame.camera.vecPosition) - 0.5f * kfArenaSize;
	gfArenaTop = XMVectorGetY(rFrame.camera.vecPosition) + 0.5f * kfArenaSize;

	for (decltype(uiMaxIndex) i = 0; i <= uiMaxIndex; ++i)
	{
		if (!pbUsed[i])
		{
			continue;
		}

		PusherInfo& rPusherInfo = pObjectInfos[i];

		int64_t iZoneStartX = static_cast<int64_t>((rPusherInfo.f2Position.x - rPusherInfo.fRadius - gfArenaLeft) / kfZoneSize);
		int64_t iZoneEndX = static_cast<int64_t>((rPusherInfo.f2Position.x + rPusherInfo.fRadius - gfArenaLeft) / kfZoneSize);
		int64_t iZoneStartY = static_cast<int64_t>(-(rPusherInfo.f2Position.y + rPusherInfo.fRadius - gfArenaTop) / kfZoneSize);
		int64_t iZoneEndY = static_cast<int64_t>(-(rPusherInfo.f2Position.y - rPusherInfo.fRadius - gfArenaTop) / kfZoneSize);

		if (iZoneStartX >= kiZones || iZoneEndX < 0 || iZoneStartY >= kiZones || iZoneEndY < 0)
		{
			continue;
		}

		iZoneStartX = std::clamp(iZoneStartX, 0ll, kiZones - 1);
		iZoneEndX = std::clamp(iZoneEndX, 0ll, kiZones - 1);
		iZoneStartY = std::clamp(iZoneStartY, 0ll, kiZones - 1);
		iZoneEndY = std::clamp(iZoneEndY, 0ll, kiZones - 1);

		for (int64_t y = iZoneStartY; y <= iZoneEndY; ++y)
		{
			for (int64_t x = iZoneStartX; x <= iZoneEndX; ++x)
			{
				int64_t iPushersPerZone = gppuiPushersPerZone[x][y];
				if (iPushersPerZone >= kiMaxPushersPerZone) [[unlikely]]
				{
					DEBUG_BREAK();
					continue;
				}

				gpppuiPusherZones[x][y][iPushersPerZone] = static_cast<std::remove_reference<decltype(gpppuiPusherZones[0][0][0])>::type>(i);
				++gppuiPushersPerZone[x][y];
				
			#if 0
				static uint16_t suiMax = 0;
				if (gppuiPushersPerZone[x][y] > suiMax) [[unlikely]]
				{
					suiMax = gppuiPushersPerZone[x][y];
					LOG("Max: {}", suiMax);
				}
			#endif
			}
		}
	}
}

XMVECTOR XM_CALLCONV Pushers::ApplyPush(FXMVECTOR vecPosition, pusher_t uiIgnorePusher, PusherFlags_t includeFlags, PusherFlags_t excludeFlags)
{
#if defined(BT_DEBUG)
	ASSERT(gCurrentFrameTypeProcessing == FrameType::kFull);
#endif

	auto vecPosition2d = XMVectorSetZ(vecPosition, 0.0f);
	XMFLOAT2A f2Position {};
	XMStoreFloat2A(&f2Position, vecPosition);

	// Called in PostRender so SetupZones has been called
	int64_t iZoneX = std::clamp(static_cast<int64_t>((f2Position.x - gfArenaLeft) / kfZoneSize), 0ll, kiZones - 1);
	int64_t iZoneY = std::clamp(static_cast<int64_t>(-(f2Position.y - gfArenaTop) / kfZoneSize), 0ll, kiZones - 1);
	pusher_t* puiZone = gpppuiPusherZones[iZoneX][iZoneY];
	int64_t iPushersInZone = gppuiPushersPerZone[iZoneX][iZoneY];

	auto vecPush = XMVectorZero();
	for (int64_t i = 0; i < iPushersInZone; ++i)
	{
		if (puiZone[i] == uiIgnorePusher) [[unlikely]]
		{
			continue;
		}

		PusherInfo& rPusherInfo = GetInfo(puiZone[i]);

		if (rPusherInfo.flags & excludeFlags) [[unlikely]]
		{
			continue;
		}

		if (!(rPusherInfo.flags & includeFlags)) [[unlikely]]
		{
			continue;
		}

		auto vecPusherPosition = XMVectorSetW(XMLoadFloat2(&rPusherInfo.f2Position), 1.0f);
		if (XMVector3NearEqual(vecPosition2d, vecPusherPosition, XMVectorReplicate(kfEpsilon))) [[unlikely]]
		{
			continue;
		}
		auto vecFromPusher = XMVectorSubtract(vecPosition2d, vecPusherPosition);
		auto vecDistanceSquared = XMVector3LengthSq(vecFromPusher);
		if (XMVectorGetX(vecDistanceSquared) > rPusherInfo.fRadius * rPusherInfo.fRadius) [[likely]]
		{
			continue;
		}

		auto vecIntensity = XMVectorSubtract(XMVectorReplicate(1.0f), XMVectorDivide(vecDistanceSquared, XMVectorReplicate(rPusherInfo.fRadius * rPusherInfo.fRadius)));
		vecIntensity = XMVectorPow(vecIntensity, XMVectorReplicate(rPusherInfo.fPower));
		vecIntensity = XMVectorMultiply(XMVectorReplicate(rPusherInfo.fIntensity), vecIntensity);

		auto vecFromPusherNormal = XMVector3Normalize(vecFromPusher);
		vecPush = XMVectorMultiplyAdd(vecIntensity, vecFromPusherNormal, vecPush);
	}

	return vecPush;
}

} // namespace engine
