#include "Pch.h"

#include "Collision.h"

#include "Frame/HealthDamage.h"

namespace engine
{

using enum CollisionFlags;

size_t Collision::AddLayer(const CollisionLayer& rLayer)
{
	size_t uiLayerIndex = sLayers.size();
	sLayers.push_back(rLayer);
	return uiLayerIndex;
}

void Collision::Collide()
{
	// Clear previous frame results
	sResults.clear();

	// Clear kAlreadyCollided flags from previous frame
	for (CollisionLayer& rLayer : sLayers)
	{
		if (rLayer.pFlags != nullptr)
		{
			for (int64_t i = 0; i < rLayer.iCount; ++i)
			{
				rLayer.pFlags[i].Clear(kAlreadyCollided);
			}
		}
	}

	// Process all compatible layer pairs
	for (size_t uiLayerA = 0; uiLayerA < sLayers.size(); ++uiLayerA)
	{
		for (size_t uiLayerB = uiLayerA + 1; uiLayerB < sLayers.size(); ++uiLayerB)
		{
			// Check if layers can collide with each other
			bool bACollidesWithB = (sLayers.at(uiLayerA).uiCollidesWith & sLayers.at(uiLayerB).uiCategory) != 0;
			bool bBCollidesWithA = (sLayers.at(uiLayerB).uiCollidesWith & sLayers.at(uiLayerA).uiCategory) != 0;

			if (!bACollidesWithB && !bBCollidesWithA)
			{
				continue;
			}

			CollideLayerPair(uiLayerA, uiLayerB, bACollidesWithB, bBCollidesWithA);
		}
	}
}

void Collision::CollideLayerPair(size_t uiLayerA, size_t uiLayerB, bool bACollidesWithB, bool bBCollidesWithA)
{
	CollisionLayer& rLayerA = sLayers.at(uiLayerA);
	CollisionLayer& rLayerB = sLayers.at(uiLayerB);

	for (int64_t i = 0; i < rLayerA.iCount; ++i)
	{
		// Get flags for A
		CollisionFlags_t flagsA = rLayerA.pFlags != nullptr ? rLayerA.pFlags[i] : rLayerA.uniformFlags;

		// Skip if already collided this frame (for destroy-on-collide objects)
		if (flagsA & kAlreadyCollided)
		{
			continue;
		}

		// Get position and radius for A
		XMVECTOR vecPositionA = rLayerA.pVecPositions[i];
		float fRadiusA = rLayerA.pfRadii != nullptr ? rLayerA.pfRadii[i] : rLayerA.fUniformRadius;

		for (int64_t j = 0; j < rLayerB.iCount; ++j)
		{
			// Get flags for B
			CollisionFlags_t flagsB = rLayerB.pFlags != nullptr ? rLayerB.pFlags[j] : rLayerB.uniformFlags;

			// Skip if already collided this frame (for destroy-on-collide objects)
			if (flagsB & kAlreadyCollided)
			{
				continue;
			}

			// Get position and radius for B
			XMVECTOR vecPositionB = rLayerB.pVecPositions[j];
			float fRadiusB = rLayerB.pfRadii != nullptr ? rLayerB.pfRadii[j] : rLayerB.fUniformRadius;

			// Distance check
			XMVECTOR vecDiff = XMVectorSubtract(vecPositionA, vecPositionB);
			float fDistanceSquared = XMVectorGetX(XMVector3LengthSq(vecDiff));
			float fCombinedRadius = fRadiusA + fRadiusB;

			if (fDistanceSquared > fCombinedRadius * fCombinedRadius)
			{
				continue;
			}

			// Calculate contact point (midpoint between surfaces)
			float fDistance = std::sqrt(fDistanceSquared);
			XMVECTOR vecContactPoint = vecPositionA;
			if (fDistance > 0.0f)
			{
				XMVECTOR vecDirection = XMVectorScale(vecDiff, 1.0f / fDistance);
				vecContactPoint = XMVectorSubtract(vecPositionA, XMVectorScale(vecDirection, fRadiusA));
			}

			// Record collision for A (if A cares about B)
			if (bACollidesWithB)
			{
				float fDamageB = rLayerB.pfDamages != nullptr ? rLayerB.pfDamages[j] : rLayerB.fUniformDamage;

				uint64_t uiKeyA = (static_cast<uint64_t>(uiLayerA) << 32) | (static_cast<uint64_t>(i) & 0xFFFFFFFF);
				sResults[uiKeyA].push_back(
				{
					.iOtherIndex = j,
					.uiOtherLayerIndex = uiLayerB,
					.uiOtherCategory = rLayerB.uiCategory,
					.fDamageReceived = fDamageB,
					.vecContactPoint = vecContactPoint,
				});

				// Mark A as already collided if it's destroy-on-collide
				if (flagsA & kDestroyOnCollide)
				{
					if (rLayerA.pFlags != nullptr)
					{
						rLayerA.pFlags[i] |= kAlreadyCollided;
					}
				}
			}

			// Record collision for B (if B cares about A)
			if (bBCollidesWithA)
			{
				float fDamageA = rLayerA.pfDamages != nullptr ? rLayerA.pfDamages[i] : rLayerA.fUniformDamage;

				uint64_t uiKeyB = (static_cast<uint64_t>(uiLayerB) << 32) | (static_cast<uint64_t>(j) & 0xFFFFFFFF);
				sResults[uiKeyB].push_back(
				{
					.iOtherIndex = i,
					.uiOtherLayerIndex = uiLayerA,
					.uiOtherCategory = rLayerA.uiCategory,
					.fDamageReceived = fDamageA,
					.vecContactPoint = vecContactPoint,
				});

				// Mark B as already collided if it's destroy-on-collide
				if (flagsB & kDestroyOnCollide)
				{
					if (rLayerB.pFlags != nullptr)
					{
						rLayerB.pFlags[j] |= kAlreadyCollided;
					}
				}
			}
		}
	}
}

void Collision::Clear()
{
	sLayers.clear();
}

bool Collision::HasCollision(size_t uiLayerIndex, int64_t iIndex)
{
	uint64_t uiKey = (static_cast<uint64_t>(uiLayerIndex) << 32) | (static_cast<uint64_t>(iIndex) & 0xFFFFFFFF);
	return sResults.contains(uiKey);
}

const std::vector<CollisionResult>* Collision::GetCollisions(size_t uiLayerIndex, int64_t iIndex)
{
	uint64_t uiKey = (static_cast<uint64_t>(uiLayerIndex) << 32) | (static_cast<uint64_t>(iIndex) & 0xFFFFFFFF);
	auto it = sResults.find(uiKey);
	if (it != sResults.end())
	{
		return &it->second;
	}
	return nullptr;
}

} // namespace engine
