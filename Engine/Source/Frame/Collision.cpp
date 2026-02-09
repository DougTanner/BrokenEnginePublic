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

	// Map world position to grid indices with clamping
	int32_t iZoneStartX = std::clamp(static_cast<int32_t>((f4Position.x - fRadius - sfAreaMinX) / sfZoneWidth), 0, kiCollisionZonesX - 1);
	int32_t iZoneEndX = std::clamp(static_cast<int32_t>((f4Position.x + fRadius - sfAreaMinX) / sfZoneWidth), 0, kiCollisionZonesX - 1);
	int32_t iZoneStartY = std::clamp(static_cast<int32_t>((f4Position.y - fRadius - sfAreaMinY) / sfZoneHeight), 0, kiCollisionZonesY - 1);
	int32_t iZoneEndY = std::clamp(static_cast<int32_t>((f4Position.y + fRadius - sfAreaMinY) / sfZoneHeight), 0, kiCollisionZonesY - 1);

	// Insert into all overlapping zones
	for (int32_t y = iZoneStartY; y <= iZoneEndY; ++y)
	{
		for (int32_t x = iZoneStartX; x <= iZoneEndX; ++x)
		{
			ZonePair& rZonePair = rPairZones.zones[y][x];
			if (bIsLayerA)
			{
				if (rZonePair.iCountA >= static_cast<int64_t>(rZonePair.indicesA.size()))
				{
					common::DebugBreak();
					rZonePair.indicesA.resize(rZonePair.iCountA * 2);
				}
				rZonePair.indicesA.at(static_cast<size_t>(rZonePair.iCountA)) = iIndex;
				++rZonePair.iCountA;
			}
			else
			{
				if (rZonePair.iCountB >= static_cast<int64_t>(rZonePair.indicesB.size()))
				{
					common::DebugBreak();
					rZonePair.indicesB.resize(rZonePair.iCountB * 2);
				}
				rZonePair.indicesB.at(static_cast<size_t>(rZonePair.iCountB)) = iIndex;
				++rZonePair.iCountB;
			}
		}
	}
}

void Collision::SetupZones(FXMVECTOR vecArea)
{
	// Compute zone dimensions from vecArea (x=minX, y=maxY, z=maxX, w=minY)
	XMFLOAT4A f4Area {};
	XMStoreFloat4A(&f4Area, vecArea);
	sfAreaMinX = f4Area.x;
	sfAreaMinY = f4Area.w;
	sfZoneWidth = (f4Area.z - f4Area.x) / kiCollisionZonesX;
	sfZoneHeight = (f4Area.y - f4Area.w) / kiCollisionZonesY;

	// Reset zone counts instead of clearing
	for (LayerPairZones& rPairZones : sLayerPairZones)
	{
		for (ZonePair (&rRow)[kiCollisionZonesX] : rPairZones.zones)
		{
			for (ZonePair& rZonePair : rRow)
			{
				rZonePair.iCountA = 0;
				rZonePair.iCountB = 0;
			}
		}
	}

	// Same-layer collision could be supported but needs implementation (avoid self-collision, different loop structure)
	for (size_t uiLayer = 0; uiLayer < sLayers.size(); ++uiLayer)
	{
		ASSERT((sLayers.at(uiLayer).uiCollidesWith & sLayers.at(uiLayer).uiCategory) == 0 && "Same-layer collision not implemented");
	}

	// Determine active layer pairs and build zones for each
	size_t uiPairIndex = 0;
	for (size_t uiLayerA = 0; uiLayerA < sLayers.size(); ++uiLayerA)
	{
		for (size_t uiLayerB = uiLayerA + 1; uiLayerB < sLayers.size(); ++uiLayerB)
		{
			// Check if layers can collide with each other
			bool bACollidesWithB = (sLayers.at(uiLayerA).uiCollidesWith & sLayers.at(uiLayerB).uiCategory) != 0;
			bool bBCollidesWithA = (sLayers.at(uiLayerB).uiCollidesWith & sLayers.at(uiLayerA).uiCategory) != 0;

			// Assert bi-directionality: if either direction allows collision, both should
			ASSERT(bACollidesWithB == bBCollidesWithA && "Collision masks must be bi-directional");

			if (!bACollidesWithB)
			{
				continue;
			}

			// Reuse existing entry or create new one
			if (uiPairIndex >= sLayerPairZones.size())
			{
				sLayerPairZones.emplace_back();
			}
			LayerPairZones& rPairZones = sLayerPairZones.at(uiPairIndex);
			rPairZones.uiLayerA = uiLayerA;
			rPairZones.uiLayerB = uiLayerB;

			const CollisionLayer& rLayerA = sLayers.at(uiLayerA);
			const CollisionLayer& rLayerB = sLayers.at(uiLayerB);

			// Insert layer A objects
			for (int64_t i = 0; i < rLayerA.iCount; ++i)
			{
				if (rLayerA.pFlags[i] & kAlreadyCollided)
				{
					continue;
				}

				InsertIntoZones(rPairZones, i, rLayerA.pVecPositions[i], rLayerA.pfRadii[i], true);
			}

			// Insert layer B objects
			for (int64_t i = 0; i < rLayerB.iCount; ++i)
			{
				if (rLayerB.pFlags[i] & kAlreadyCollided)
				{
					continue;
				}

				InsertIntoZones(rPairZones, i, rLayerB.pVecPositions[i], rLayerB.pfRadii[i], false);
			}

			++uiPairIndex;
		}
	}
	sLayerPairZones.resize(uiPairIndex);
}

void Collision::Collide(const Alignments& rAlignments, FXMVECTOR vecArea)
{
	ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderCollide);

	// Clear previous frame results
	sResults.clear();

	// Build zone acceleration structure (includes layer pair filtering and same-layer collision assert)
	SetupZones(vecArea);

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

	CollisionLayer& rLayerA = sLayers.at(uiLayerA);
	CollisionLayer& rLayerB = sLayers.at(uiLayerB);

	// Track tested B objects to avoid duplicates from multi-zone presence
	thread_local std::vector<bool> sTestedB;
	sTestedB.assign(static_cast<size_t>(rLayerB.iCount), false);

	for (int64_t i = 0; i < rLayerA.iCount; ++i)
	{
		// Skip if already collided this frame (for destroy-on-collide objects)
		if (rLayerA.pFlags[i] & kAlreadyCollided)
		{
			continue;
		}

		// Get position and radius for A
		XMVECTOR vecPositionA = rLayerA.pVecPositions[i];
		float fRadiusA = rLayerA.pfRadii[i];

		// Calculate A's zone range with clamping
		XMFLOAT4A f4PositionA {};
		XMStoreFloat4A(&f4PositionA, vecPositionA);

		int32_t iZoneStartX = std::clamp(static_cast<int32_t>((f4PositionA.x - fRadiusA - sfAreaMinX) / sfZoneWidth), 0, kiCollisionZonesX - 1);
		int32_t iZoneEndX = std::clamp(static_cast<int32_t>((f4PositionA.x + fRadiusA - sfAreaMinX) / sfZoneWidth), 0, kiCollisionZonesX - 1);
		int32_t iZoneStartY = std::clamp(static_cast<int32_t>((f4PositionA.y - fRadiusA - sfAreaMinY) / sfZoneHeight), 0, kiCollisionZonesY - 1);
		int32_t iZoneEndY = std::clamp(static_cast<int32_t>((f4PositionA.y + fRadiusA - sfAreaMinY) / sfZoneHeight), 0, kiCollisionZonesY - 1);

		// Clear tested flags for this A object
		std::fill(sTestedB.begin(), sTestedB.end(), false);

		// Iterate zones in A's range
		for (int32_t y = iZoneStartY; y <= iZoneEndY; ++y)
		{
			for (int32_t x = iZoneStartX; x <= iZoneEndX; ++x)
			{
				ZonePair& rZonePair = rPairZones.zones[y][x];
				if (rZonePair.iCountB == 0)
				{
					continue;
				}

				// Check all B objects in this zone (no layer filtering needed)
				for (int64_t k = 0; k < rZonePair.iCountB; ++k)
				{
					int64_t j = rZonePair.indicesB.at(static_cast<size_t>(k));
					// Skip if already tested this B object
					if (sTestedB.at(static_cast<size_t>(j)))
					{
						continue;
					}
					sTestedB.at(static_cast<size_t>(j)) = true;

					// Skip if alignments don't allow collision
					if (!rAlignments.CanCollide(rLayerA.pAlignments[i], rLayerB.pAlignments[j]))
					{
						continue;
					}

					// Skip if already collided this frame (for destroy-on-collide objects)
					if (rLayerB.pFlags[j] & kAlreadyCollided)
					{
						continue;
					}

					// Get position and radius for B
					XMVECTOR vecPositionB = rLayerB.pVecPositions[j];
					float fRadiusB = rLayerB.pfRadii[j];

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
					float fDamageB = rLayerB.pfDamages[j];

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
					if (rLayerA.pFlags[i] & kDestroyOnCollide)
					{
						rLayerA.pFlags[i] |= kAlreadyCollided;
					}

					// Record collision for B (always bidirectional per assert in SetupZones)
					float fDamageA = rLayerA.pfDamages[i];

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
					if (rLayerB.pFlags[j] & kDestroyOnCollide)
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
