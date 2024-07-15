// Note: Not using precompiled header so that this file can be optimized in Debug builds
// #pragma optimize( "", off )
#include "Pch.h"

#include "Pullers.h"

#include "Frame/Frame.h"

using namespace DirectX;

namespace engine
{

DirectX::XMVECTOR XM_CALLCONV Pullers::ApplyPull(DirectX::FXMVECTOR vecPosition)
{
#if defined(BT_DEBUG)
	ASSERT(gCurrentFrameTypeProcessing == FrameType::kFull);
#endif

	auto vecPosition2d = XMVectorSetZ(vecPosition, 0.0f);

	auto vecPull = XMVectorZero();
	for (decltype(uiMaxIndex) i = 0; i <= uiMaxIndex; ++i)
	{
		if (!pbUsed[i])
		{
			continue;
		}

		PullerInfo& rPullerInfo = pObjectInfos[i];

		auto vecPullerPosition = XMVectorSetW(XMLoadFloat2(&rPullerInfo.f2Position), 1.0f);
		auto vecFromPuller = XMVectorSubtract(vecPosition2d, vecPullerPosition);
		auto vecDistanceSquared = XMVector2LengthSq(vecFromPuller);
		float fDistanceSquared = XMVectorGetX(vecDistanceSquared);
		if (fDistanceSquared > rPullerInfo.fRadius * rPullerInfo.fRadius) [[likely]]
		{
			continue;
		}

		float fPercent = 1.0f - std::sqrt(fDistanceSquared) / rPullerInfo.fRadius;
		vecPull = XMVectorMultiplyAdd(XMVectorReplicate(rPullerInfo.fIntensity * fPercent), XMVector2Normalize(vecFromPuller), vecPull);
	}

	return vecPull;
}

} // namespace engine
