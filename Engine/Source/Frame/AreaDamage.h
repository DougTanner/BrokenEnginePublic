#pragma once

namespace engine
{

inline constexpr int64_t kiAreaDamageSourcePreallocate = 16;

struct AreaDamageSource
{
	XMVECTOR vecPosition {};
	float fRadius = 0.0f;
	float fDamage = 0.0f;
	uint16_t uiCategory = 0;
};

class AreaDamage
{
public:

	static void Add(const AreaDamageSource& rSource);

	// Returns total damage with linear falloff applied, filtered by category mask
	// Outputs the closest damage source position via rvecClosestSource
	static float Get(FXMVECTOR vecPosition, uint16_t uiCategoryMask, XMVECTOR& rvecClosestSource);

	static void Clear();

private:

	static thread_local std::vector<AreaDamageSource> sAreaDamageSources;
	static thread_local int64_t siAreaDamageSourceCount;
};

} // namespace engine
