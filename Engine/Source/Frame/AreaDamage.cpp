#include "AreaDamage.h"

namespace engine
{

thread_local std::vector<AreaDamageSource> AreaDamage::sAreaDamageSources;
thread_local int64_t AreaDamage::siAreaDamageSourceCount = 0;

void AreaDamage::Add(const AreaDamageSource& rSource)
{
	int64_t iIndex = siAreaDamageSourceCount;
	if (sAreaDamageSources.empty())
	{
		// Heap: one-time per-thread pre-allocation (thread_local vectors start empty to avoid allocating during mi_process_init)
		ScopedSuppressAllocationTracking suppress;
		sAreaDamageSources.resize(kiAreaDamageSourcePreallocate);
	}

	if (siAreaDamageSourceCount >= static_cast<int64_t>(sAreaDamageSources.size()))
	{
		LOG(kDefault, kWarning, "AreaDamage: sAreaDamageSources overflow (count: {}, capacity: {}). Increase kiAreaDamageSourcePreallocate in AreaDamage.h", siAreaDamageSourceCount, sAreaDamageSources.size());
		DEBUG_BREAK();
		// Heap: rare growth when area-damage source count exceeds pre-allocation
		ScopedSuppressAllocationTracking suppress;
		sAreaDamageSources.resize(siAreaDamageSourceCount * 2);
	}
	sAreaDamageSources.at(static_cast<size_t>(iIndex)) = rSource;
	++siAreaDamageSourceCount;
}

float AreaDamage::Get(FXMVECTOR vecPosition, uint16_t uiCategoryMask, XMVECTOR& rvecClosestSource)
{
	float fTotalDamage = 0.0f;
	float fClosestDistance = std::numeric_limits<float>::max();
	rvecClosestSource = vecPosition;

	for (int64_t i = 0; i < siAreaDamageSourceCount; ++i)
	{
		const AreaDamageSource& rSource = sAreaDamageSources.at(static_cast<size_t>(i));
		// Filter by category
		if ((rSource.uiCategory & uiCategoryMask) == 0)
		{
			continue;
		}

		// Calculate distance
		XMVECTOR vecDiff = XMVectorSubtract(vecPosition, rSource.vecPosition);
		float fDistance = XMVectorGetX(XMVector3Length(vecDiff));

		// Skip if outside radius
		if (fDistance >= rSource.fRadius)
		{
			continue;
		}

		// Track closest source
		if (fDistance < fClosestDistance)
		{
			fClosestDistance = fDistance;
			rvecClosestSource = rSource.vecPosition;
		}

		// Linear falloff: full damage at center, zero at edge
		float fFalloff = 1.0f - (fDistance / rSource.fRadius);
		fTotalDamage += rSource.fDamage * fFalloff;
	}

	return fTotalDamage;
}

void AreaDamage::Clear()
{
	siAreaDamageSourceCount = 0;
}

} // namespace engine
