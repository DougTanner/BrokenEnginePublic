#include "Colliders.h"

#include "Frame/FrameBase.h"

namespace engine
{

void CollidersInterpolate::Update([[maybe_unused]] CollidersInterpolate& __restrict rCurrent, [[maybe_unused]] const CollidersInterpolate& __restrict rPrevious)
{
	ReallocateAndCopyMetadata(rCurrent, rPrevious, rCurrent.Members());

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		float fRadius = rPrevious.pfRadii[i];
		uint16_t uiCategory = rPrevious.puiCategories[i];
		uint16_t uiCollidesWith = rPrevious.puiCollidesWith[i];
		uint8_t uiFlags = rPrevious.puiFlags[i];
		float fDamage = rPrevious.pfDamages[i];

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfRadii[i] = fRadius;
		rCurrent.puiCategories[i] = uiCategory;
		rCurrent.puiCollidesWith[i] = uiCollidesWith;
		rCurrent.puiFlags[i] = uiFlags;
		rCurrent.pfDamages[i] = fDamage;
	}
}

bool CollidersInterpolate::operator==(const CollidersInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pfRadii[i], rOther.pfRadii[i]);
		bEqual &= common::BreakOnNotEqual(puiCategories[i], rOther.puiCategories[i]);
		bEqual &= common::BreakOnNotEqual(puiCollidesWith[i], rOther.puiCollidesWith[i]);
		bEqual &= common::BreakOnNotEqual(puiFlags[i], rOther.puiFlags[i]);
		bEqual &= common::BreakOnNotEqual(pfDamages[i], rOther.pfDamages[i]);
	}

	return bEqual;
}

void CollidersPostRender::Update([[maybe_unused]] CollidersPostRender& __restrict rCurrent, [[maybe_unused]] const CollidersPostRender& __restrict rPrevious)
{
	ReallocateAndCopyMetadata(rCurrent, rPrevious, rCurrent.Members());

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		collider_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

bool CollidersPostRender::operator==(const CollidersPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

bool CollidersPostRender::HasCollision(collider_t id)
{
	return sCollisionResults.contains(id);
}

const std::vector<CollisionResult>* CollidersPostRender::GetCollisions(collider_t id)
{
	auto it = sCollisionResults.find(id);
	if (it != sCollisionResults.end())
	{
		return &it->second;
	}
	return nullptr;
}

collider_t CollidersPostRender::Add(CollidersInterpolate& rInterpolate, CollidersPostRender& rPostRender, FramePostRenderBase& rFramePostRender,
                                    FXMVECTOR vecPosition, float fRadius, uint16_t uiCategory, uint16_t uiCollidesWith, uint8_t uiFlags, float fDamage)
{
	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());

	auto [iSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFramePostRender);

	// Initialize interpolate data
	rInterpolate.pVecPositions[iSpawnIndex] = vecPosition;
	rInterpolate.pfRadii[iSpawnIndex] = fRadius;
	rInterpolate.puiCategories[iSpawnIndex] = uiCategory;
	rInterpolate.puiCollidesWith[iSpawnIndex] = uiCollidesWith;
	rInterpolate.puiFlags[iSpawnIndex] = uiFlags;
	rInterpolate.pfDamages[iSpawnIndex] = fDamage;

	// Store ID in postRender for removal lookup
	rPostRender.puiIds[iSpawnIndex] = newId;

	return newId;
}

void CollidersPostRender::Remove(CollidersInterpolate& rInterpolate, CollidersPostRender& rPostRender, collider_t id)
{
	RemoveIndexableElement(rInterpolate, rPostRender, id, rInterpolate.Members(), rPostRender.Members());
}

void CollidersPostRender::UpdatePosition(CollidersInterpolate& rInterpolate, collider_t id, FXMVECTOR vecPosition)
{
	uint64_t uiIndex = rInterpolate.IdToIndex(id);
	rInterpolate.pVecPositions[uiIndex] = vecPosition;
}

void CollidersPostRender::Collide(CollidersInterpolate& rInterpolate, CollidersPostRender& rPostRender)
{
	sCollisionResults.clear();

	// Clear kAlreadyCollided from previous frame
	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		rInterpolate.puiFlags[i] &= ~game::ColliderFlags::kAlreadyCollided;
	}

	// O(n²) pairwise collision check with early filtering
	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		// Skip if already collided this frame
		if (rInterpolate.puiFlags[i] & game::ColliderFlags::kAlreadyCollided)
		{
			continue;
		}

		uint16_t uiCategoryA = rInterpolate.puiCategories[i];
		uint16_t uiCollidesWithA = rInterpolate.puiCollidesWith[i];

		for (int64_t j = i + 1; j < rInterpolate.iCount; ++j)
		{
			// Skip if already collided this frame
			if (rInterpolate.puiFlags[j] & game::ColliderFlags::kAlreadyCollided)
			{
				continue;
			}

			uint16_t uiCategoryB = rInterpolate.puiCategories[j];
			uint16_t uiCollidesWithB = rInterpolate.puiCollidesWith[j];

			// Early filter: check compatibility before distance calc
			bool bACanHitB = (uiCategoryB & uiCollidesWithA) != 0;
			bool bBCanHitA = (uiCategoryA & uiCollidesWithB) != 0;
			if (!bACanHitB && !bBCanHitA)
			{
				continue;
			}

			float fDistance = common::Distance(rInterpolate.pVecPositions[i], rInterpolate.pVecPositions[j]);
			float fCombinedRadius = rInterpolate.pfRadii[i] + rInterpolate.pfRadii[j];

			if (fDistance < fCombinedRadius)
			{
				collider_t idA = rPostRender.puiIds[i];
				collider_t idB = rPostRender.puiIds[j];

				XMVECTOR vecContactPoint = XMVectorLerp(rInterpolate.pVecPositions[i], rInterpolate.pVecPositions[j], 0.5f);

				// Only report collision to A if B can hit A
				if (bBCanHitA)
				{
					sCollisionResults[idA].push_back({
						.otherId = idB,
						.uiOtherCategory = uiCategoryB,
						.fDamageReceived = rInterpolate.pfDamages[j],
						.vecContactPoint = vecContactPoint,
					});
				}

				// Only report collision to B if A can hit B
				if (bACanHitB)
				{
					sCollisionResults[idB].push_back({
						.otherId = idA,
						.uiOtherCategory = uiCategoryA,
						.fDamageReceived = rInterpolate.pfDamages[i],
						.vecContactPoint = vecContactPoint,
					});
				}

				// Mark destroy-on-collide objects as already collided
				if (rInterpolate.puiFlags[i] & game::ColliderFlags::kDestroyOnCollide)
				{
					rInterpolate.puiFlags[i] |= game::ColliderFlags::kAlreadyCollided;
				}
				if (rInterpolate.puiFlags[j] & game::ColliderFlags::kDestroyOnCollide)
				{
					rInterpolate.puiFlags[j] |= game::ColliderFlags::kAlreadyCollided;
				}
			}
		}
	}
}

} // namespace engine
