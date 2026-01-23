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

void Collision::SetupZones()
{
	// Remove zones that were empty after last frame
	std::erase_if(sZones, [](const auto& rZone) { return rZone.second.empty(); });

	// Clear zone vectors but keep map structure for reuse
	for ([[maybe_unused]] auto& [iZoneKey, rEntries] : sZones)
	{
		// Shrink if contents are less than half capacity
		if (rEntries.size() < rEntries.capacity() / 2)
		{
			rEntries.shrink_to_fit();
		}
		rEntries.clear();
	}

	// Insert all objects into zones
	for (size_t uiLayer = 0; uiLayer < sLayers.size(); ++uiLayer)
	{
		const CollisionLayer& rLayer = sLayers[uiLayer];

		for (int64_t i = 0; i < rLayer.iCount; ++i)
		{
			// Skip if already collided
			CollisionFlags_t flags = rLayer.pFlags != nullptr ? rLayer.pFlags[i] : rLayer.uniformFlags;
			if (flags & kAlreadyCollided)
			{
				continue;
			}

			XMFLOAT4A f4Position {};
			XMStoreFloat4A(&f4Position, rLayer.pVecPositions[i]);
			float fRadius = rLayer.pfRadii != nullptr ? rLayer.pfRadii[i] : rLayer.fUniformRadius;

			// Calculate zone bounds from position +/- radius (origin-relative)
			int32_t iZoneStartX = static_cast<int32_t>(std::floor((f4Position.x - fRadius) / kfCollisionZoneSize));
			int32_t iZoneEndX = static_cast<int32_t>(std::floor((f4Position.x + fRadius) / kfCollisionZoneSize));
			int32_t iZoneStartY = static_cast<int32_t>(std::floor((f4Position.y - fRadius) / kfCollisionZoneSize));
			int32_t iZoneEndY = static_cast<int32_t>(std::floor((f4Position.y + fRadius) / kfCollisionZoneSize));

			// Pack layer and index
			uint32_t uiEntry = (static_cast<uint32_t>(uiLayer) << 24) | (static_cast<uint32_t>(i) & 0x00FFFFFF);

			// Insert into all overlapping zones
			for (int32_t y = iZoneStartY; y <= iZoneEndY; ++y)
			{
				for (int32_t x = iZoneStartX; x <= iZoneEndX; ++x)
				{
					int64_t iZoneKey = (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(y);
					sZones[iZoneKey].push_back(uiEntry);
				}
			}
		}
	}
}

void Collision::Collide(const CollisionGroups& rCollisionGroups)
{
	ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderCollide);

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

	// Build zone acceleration structure
	SetupZones();

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

			CollideLayerPair(rCollisionGroups, uiLayerA, uiLayerB, bACollidesWithB, bBCollidesWithA);
		}
	}
}

void Collision::CollideLayerPair(const CollisionGroups& rCollisionGroups, size_t uiLayerA, size_t uiLayerB, bool bACollidesWithB, bool bBCollidesWithA)
{
	CollisionLayer& rLayerA = sLayers.at(uiLayerA);
	CollisionLayer& rLayerB = sLayers.at(uiLayerB);

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
				auto it = sZones.find(iZoneKey);
				if (it == sZones.end())
				{
					continue;
				}

				// Check all entries in this zone
				for (uint32_t uiEntry : it->second)
				{
					// Unpack entry
					size_t uiEntryLayer = uiEntry >> 24;
					int64_t iEntryIndex = static_cast<int64_t>(uiEntry & 0x00FFFFFF);

					// Skip if not from layer B
					if (uiEntryLayer != uiLayerB)
					{
						continue;
					}

					int64_t j = iEntryIndex;

					// Skip if already tested this B object
					if (sTestedB[static_cast<size_t>(j)])
					{
						continue;
					}
					sTestedB[static_cast<size_t>(j)] = true;

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

					// Get groups for A and B
					collision_group_t groupA = rLayerA.pGroups != nullptr ? rLayerA.pGroups[i] : rLayerA.uniformGroup;
					collision_group_t groupB = rLayerB.pGroups != nullptr ? rLayerB.pGroups[j] : rLayerB.uniformGroup;

					// Skip if groups don't allow collision
					if (!rCollisionGroups.CanCollide(groupA, groupB))
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
