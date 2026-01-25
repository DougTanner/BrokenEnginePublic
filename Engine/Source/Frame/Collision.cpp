#include "Pch.h"

#include "Collision.h"

#include "Frame/HealthDamage.h"
#include "Profile/ProfileManager.h"

namespace engine
{

using enum CollisionFlags;

size_t Collision::AddLayer(const CollisionLayer& rLayer)
{
	size_t uiLayerIndex = sLayers.size();
	sLayers.push_back(rLayer);
	return uiLayerIndex;
}

void Collision::InsertIntoZones(LayerPairZones& rPairZones, int64_t iIndex, FXMVECTOR vecPosition, float fRadius, bool bIsLayerA)
{
	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);

	// Calculate zone bounds from position +/- radius (origin-relative)
	int32_t iZoneStartX = static_cast<int32_t>(std::floor((f4Position.x - fRadius) / kfCollisionZoneSize));
	int32_t iZoneEndX = static_cast<int32_t>(std::floor((f4Position.x + fRadius) / kfCollisionZoneSize));
	int32_t iZoneStartY = static_cast<int32_t>(std::floor((f4Position.y - fRadius) / kfCollisionZoneSize));
	int32_t iZoneEndY = static_cast<int32_t>(std::floor((f4Position.y + fRadius) / kfCollisionZoneSize));

	// Insert into all overlapping zones
	for (int32_t y = iZoneStartY; y <= iZoneEndY; ++y)
	{
		for (int32_t x = iZoneStartX; x <= iZoneEndX; ++x)
		{
			int64_t iZoneKey = (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(y);
			ZonePair& rZonePair = rPairZones.zones[iZoneKey];
			if (bIsLayerA)
			{
				rZonePair.indicesA.push_back(iIndex);
			}
			else
			{
				rZonePair.indicesB.push_back(iIndex);
			}
		}
	}
}

void Collision::SetupZones()
{
	sLayerPairZones.clear();

	// Same-layer collision could be supported but needs implementation (avoid self-collision, different loop structure)
	for (size_t uiLayer = 0; uiLayer < sLayers.size(); ++uiLayer)
	{
		Assert((sLayers[uiLayer].uiCollidesWith & sLayers[uiLayer].uiCategory) == 0 && "Same-layer collision not implemented");
	}

	// Determine active layer pairs and build zones for each
	for (size_t uiLayerA = 0; uiLayerA < sLayers.size(); ++uiLayerA)
	{
		for (size_t uiLayerB = uiLayerA + 1; uiLayerB < sLayers.size(); ++uiLayerB)
		{
			// Check if layers can collide with each other
			bool bACollidesWithB = (sLayers[uiLayerA].uiCollidesWith & sLayers[uiLayerB].uiCategory) != 0;
			bool bBCollidesWithA = (sLayers[uiLayerB].uiCollidesWith & sLayers[uiLayerA].uiCategory) != 0;

			// Assert bi-directionality: if either direction allows collision, both should
			Assert(bACollidesWithB == bBCollidesWithA && "Collision masks must be bi-directional");

			if (!bACollidesWithB)
			{
				continue;
			}

			// Create zone structure for this layer pair
			LayerPairZones& rPairZones = sLayerPairZones.emplace_back();
			rPairZones.uiLayerA = uiLayerA;
			rPairZones.uiLayerB = uiLayerB;

			const CollisionLayer& rLayerA = sLayers[uiLayerA];
			const CollisionLayer& rLayerB = sLayers[uiLayerB];

			// Insert layer A objects
			for (int64_t i = 0; i < rLayerA.iCount; ++i)
			{
				CollisionFlags_t flags = rLayerA.pFlags != nullptr ? rLayerA.pFlags[i] : rLayerA.uniformFlags;
				if (flags & kAlreadyCollided)
				{
					continue;
				}

				float fRadius = rLayerA.pfRadii != nullptr ? rLayerA.pfRadii[i] : rLayerA.fUniformRadius;
				InsertIntoZones(rPairZones, i, rLayerA.pVecPositions[i], fRadius, true);
			}

			// Insert layer B objects
			for (int64_t i = 0; i < rLayerB.iCount; ++i)
			{
				CollisionFlags_t flags = rLayerB.pFlags != nullptr ? rLayerB.pFlags[i] : rLayerB.uniformFlags;
				if (flags & kAlreadyCollided)
				{
					continue;
				}

				float fRadius = rLayerB.pfRadii != nullptr ? rLayerB.pfRadii[i] : rLayerB.fUniformRadius;
				InsertIntoZones(rPairZones, i, rLayerB.pVecPositions[i], fRadius, false);
			}
		}
	}
}

void Collision::Collide(const Alignments& rAlignments)
{
	ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderCollide);

	// Clear previous frame results
	sResults.clear();

	// Build zone acceleration structure (includes layer pair filtering and same-layer collision assert)
	SetupZones();

	// Process all layer pair zones
	for (LayerPairZones& rPairZones : sLayerPairZones)
	{
		CollideLayerPair(rAlignments, rPairZones);
	}
}

void Collision::CollideLayerPair(const Alignments& rAlignments, LayerPairZones& rPairZones)
{
	size_t uiLayerA = rPairZones.uiLayerA;
	size_t uiLayerB = rPairZones.uiLayerB;

	CollisionLayer& rLayerA = sLayers[uiLayerA];
	CollisionLayer& rLayerB = sLayers[uiLayerB];

	// Track tested B objects to avoid duplicates from multi-zone presence
	thread_local std::vector<bool> sTestedB;
	sTestedB.assign(static_cast<size_t>(rLayerB.iCount), false);

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

		// Calculate A's zone range (origin-relative)
		XMFLOAT4A f4PositionA {};
		XMStoreFloat4A(&f4PositionA, vecPositionA);

		int32_t iZoneStartX = static_cast<int32_t>(std::floor((f4PositionA.x - fRadiusA) / kfCollisionZoneSize));
		int32_t iZoneEndX = static_cast<int32_t>(std::floor((f4PositionA.x + fRadiusA) / kfCollisionZoneSize));
		int32_t iZoneStartY = static_cast<int32_t>(std::floor((f4PositionA.y - fRadiusA) / kfCollisionZoneSize));
		int32_t iZoneEndY = static_cast<int32_t>(std::floor((f4PositionA.y + fRadiusA) / kfCollisionZoneSize));

		// Clear tested flags for this A object
		std::fill(sTestedB.begin(), sTestedB.end(), false);

		// Iterate zones in A's range
		for (int32_t y = iZoneStartY; y <= iZoneEndY; ++y)
		{
			for (int32_t x = iZoneStartX; x <= iZoneEndX; ++x)
			{
				int64_t iZoneKey = (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(y);
				auto it = rPairZones.zones.find(iZoneKey);
				if (it == rPairZones.zones.end())
				{
					continue;
				}

				// Check all B objects in this zone (no layer filtering needed)
				for (int64_t j : it->second.indicesB)
				{
					// Skip if already tested this B object
					if (sTestedB[static_cast<size_t>(j)])
					{
						continue;
					}
					sTestedB[static_cast<size_t>(j)] = true;

					// Skip if alignments don't allow collision
					if (!rAlignments.CanCollide(rLayerA.pAlignments[i], rLayerB.pAlignments[j]))
					{
						continue;
					}

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

					// Record collision for A (always bidirectional per assert in SetupZones)
					float fDamageB = rLayerB.pfDamages != nullptr ? rLayerB.pfDamages[j] : rLayerB.fUniformDamage;

					uint64_t uiKeyA = (static_cast<uint64_t>(uiLayerA) << 32) | (static_cast<uint64_t>(i) & 0xFFFFFFFF);
					sResults[uiKeyA].push_back(
					{
						.iOtherIndex = j,
						.uiOtherLayerIndex = uiLayerB,
						.uiOtherCategory = rLayerB.uiCategory,
						.fDamageReceived = fDamageB,
						.vecContactPoint = vecContactPoint,
						.vecOtherVelocity = rLayerB.pVecVelocities != nullptr ? rLayerB.pVecVelocities[j] : XMVectorZero(),
					});

					// Mark A as already collided if it's destroy-on-collide
					if (flagsA & kDestroyOnCollide)
					{
						if (rLayerA.pFlags != nullptr)
						{
							rLayerA.pFlags[i] |= kAlreadyCollided;
						}
					}

					// Record collision for B (always bidirectional per assert in SetupZones)
					float fDamageA = rLayerA.pfDamages != nullptr ? rLayerA.pfDamages[i] : rLayerA.fUniformDamage;

					uint64_t uiKeyB = (static_cast<uint64_t>(uiLayerB) << 32) | (static_cast<uint64_t>(j) & 0xFFFFFFFF);
					sResults[uiKeyB].push_back(
					{
						.iOtherIndex = i,
						.uiOtherLayerIndex = uiLayerA,
						.uiOtherCategory = rLayerA.uiCategory,
						.fDamageReceived = fDamageA,
						.vecContactPoint = vecContactPoint,
						.vecOtherVelocity = rLayerA.pVecVelocities != nullptr ? rLayerA.pVecVelocities[i] : XMVectorZero(),
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
}

void Collision::Clear()
{
	sLayers.clear();
	sLayerPairZones.clear();
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

void Collision::AddAreaDamage(const AreaDamageSource& rSource)
{
	sAreaDamageSources.push_back(rSource);
}

float Collision::GetAreaDamage(FXMVECTOR vecPosition, uint16_t uiCategoryMask, XMVECTOR& rvecClosestSource)
{
	float fTotalDamage = 0.0f;
	float fClosestDistance = std::numeric_limits<float>::max();
	rvecClosestSource = vecPosition;

	for (const AreaDamageSource& rSource : sAreaDamageSources)
	{
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

void Collision::ClearAreaDamage()
{
	sAreaDamageSources.clear();
}

} // namespace engine
