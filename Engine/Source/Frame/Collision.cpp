#include "Pch.h"

#include "Collision.h"
#include "ThreadLocal.h"
#include "Frame/Frame.h"
#include "Frame/HealthDamage.h"
#include "Memory/MemoryManager.h"
#include "Profile/ProfileManager.h"

namespace engine
{

using enum CollisionFlags;

size_t Collision::AddLayer(const CollisionLayer& rLayer)
{
	size_t uiLayerIndex = static_cast<size_t>(siLayerCount);
	if (siLayerCount >= static_cast<int64_t>(sLayers.size()))
	{
		Log("Collision: sLayers overflow (count: {}, capacity: {}). Increase kiCollisionLayerPreallocate in Collision.h", siLayerCount, sLayers.size());
		DEBUG_BREAK();
		sLayers.resize(sLayers.empty() ? kiCollisionLayerPreallocate : siLayerCount * 2);
	}
	sLayers.at(uiLayerIndex) = rLayer;
	++siLayerCount;
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
					Log("Collision: ZonePair.indicesA overflow (count: {}, capacity: {}). Increase kiCollisionZonePreallocate in Collision.h", rZonePair.iCountA, rZonePair.indicesA.size());
					DEBUG_BREAK();
					rZonePair.indicesA.resize(rZonePair.iCountA * 2);
				}
				rZonePair.indicesA.at(static_cast<size_t>(rZonePair.iCountA)) = iIndex;
				++rZonePair.iCountA;
			}
			else
			{
				if (rZonePair.iCountB >= static_cast<int64_t>(rZonePair.indicesB.size()))
				{
					Log("Collision: ZonePair.indicesB overflow (count: {}, capacity: {}). Increase kiCollisionZonePreallocate in Collision.h", rZonePair.iCountB, rZonePair.indicesB.size());
					DEBUG_BREAK();
					rZonePair.indicesB.resize(rZonePair.iCountB * 2);
				}
				rZonePair.indicesB.at(static_cast<size_t>(rZonePair.iCountB)) = iIndex;
				++rZonePair.iCountB;
			}
		}
	}
}

static bool XM_CALLCONV SweptSphereTest(FXMVECTOR vecPosA, FXMVECTOR vecVelA, float fRadiusA,
                                        GXMVECTOR vecPosB, HXMVECTOR vecVelB, float fRadiusB,
                                        float& rfContactTime)
{
	XMVECTOR vecRelPos = XMVectorSubtract(vecPosB, vecPosA);
	XMVECTOR vecRelVel = XMVectorScale(XMVectorSubtract(vecVelB, vecVelA), game::kfDeltaTime);
	float fSumRadius = fRadiusA + fRadiusB;

	// Quadratic coefficients for swept intersection
	float fC = XMVectorGetX(XMVector3Dot(vecRelPos, vecRelPos)) - fSumRadius * fSumRadius;
	if (fC <= 0.0f)
	{
		// Already overlapping - fall through to discrete
		return false;
	}

	float fB = 2.0f * XMVectorGetX(XMVector3Dot(vecRelPos, vecRelVel));
	if (fB >= 0.0f)
	{
		// Moving apart
		return false;
	}

	float fA = XMVectorGetX(XMVector3Dot(vecRelVel, vecRelVel));
	if (fA < 1e-8f)
	{
		// No relative motion
		return false;
	}

	float fDiscriminant = fB * fB - 4.0f * fA * fC;
	if (fDiscriminant < 0.0f)
	{
		return false;
	}

	// Earliest contact time within the frame
	float fT = (-fB - std::sqrt(fDiscriminant)) / (2.0f * fA);
	if (fT >= 0.0f && fT <= 1.0f)
	{
		rfContactTime = fT * game::kfDeltaTime;
		return true;
	}
	return false;
}

void Collision::InsertIntoZonesSwept(LayerPairZones& rPairZones, int64_t iIndex, FXMVECTOR vecMin, FXMVECTOR vecMax, float fRadius, bool bIsLayerA)
{
	XMFLOAT4A f4Min {};
	XMFLOAT4A f4Max {};
	XMStoreFloat4A(&f4Min, vecMin);
	XMStoreFloat4A(&f4Max, vecMax);

	int32_t iZoneStartX = std::clamp(static_cast<int32_t>((f4Min.x - fRadius - sfAreaMinX) / sfZoneWidth), 0, kiCollisionZonesX - 1);
	int32_t iZoneEndX = std::clamp(static_cast<int32_t>((f4Max.x + fRadius - sfAreaMinX) / sfZoneWidth), 0, kiCollisionZonesX - 1);
	int32_t iZoneStartY = std::clamp(static_cast<int32_t>((f4Min.y - fRadius - sfAreaMinY) / sfZoneHeight), 0, kiCollisionZonesY - 1);
	int32_t iZoneEndY = std::clamp(static_cast<int32_t>((f4Max.y + fRadius - sfAreaMinY) / sfZoneHeight), 0, kiCollisionZonesY - 1);

	for (int32_t y = iZoneStartY; y <= iZoneEndY; ++y)
	{
		for (int32_t x = iZoneStartX; x <= iZoneEndX; ++x)
		{
			ZonePair& rZonePair = rPairZones.zones[y][x];
			if (bIsLayerA)
			{
				if (rZonePair.iCountA >= static_cast<int64_t>(rZonePair.indicesA.size()))
				{
					Log("Collision: ZonePair.indicesA overflow (count: {}, capacity: {}). Increase kiCollisionZonePreallocate in Collision.h", rZonePair.iCountA, rZonePair.indicesA.size());
					DEBUG_BREAK();
					rZonePair.indicesA.resize(rZonePair.iCountA * 2);
				}
				rZonePair.indicesA.at(static_cast<size_t>(rZonePair.iCountA)) = iIndex;
				++rZonePair.iCountA;
			}
			else
			{
				if (rZonePair.iCountB >= static_cast<int64_t>(rZonePair.indicesB.size()))
				{
					Log("Collision: ZonePair.indicesB overflow (count: {}, capacity: {}). Increase kiCollisionZonePreallocate in Collision.h", rZonePair.iCountB, rZonePair.indicesB.size());
					DEBUG_BREAK();
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
	for (int64_t i = 0; i < siLayerPairCount; ++i)
	{
		LayerPairZones& rPairZones = sLayerPairZones.at(static_cast<size_t>(i));
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
	for (int64_t iLayer = 0; iLayer < siLayerCount; ++iLayer)
	{
		ASSERT((sLayers.at(static_cast<size_t>(iLayer)).uiCollidesWith & sLayers.at(static_cast<size_t>(iLayer)).uiCategory) == 0 && "Same-layer collision not implemented");
	}

	// Determine active layer pairs and build zones for each
	size_t uiPairIndex = 0;
	for (int64_t iLayerA = 0; iLayerA < siLayerCount; ++iLayerA)
	{
		for (int64_t iLayerB = iLayerA + 1; iLayerB < siLayerCount; ++iLayerB)
		{
			// Check if layers can collide with each other
			bool bACollidesWithB = (sLayers.at(static_cast<size_t>(iLayerA)).uiCollidesWith & sLayers.at(static_cast<size_t>(iLayerB)).uiCategory) != 0;
			bool bBCollidesWithA = (sLayers.at(static_cast<size_t>(iLayerB)).uiCollidesWith & sLayers.at(static_cast<size_t>(iLayerA)).uiCategory) != 0;

			// Assert bi-directionality: if either direction allows collision, both should
			ASSERT(bACollidesWithB == bBCollidesWithA && "Collision masks must be bi-directional");

			if (!bACollidesWithB)
			{
				continue;
			}

			// Reuse existing entry or grow if needed
			if (uiPairIndex >= sLayerPairZones.size())
			{
				Log("Collision: sLayerPairZones overflow (index: {}, capacity: {}). Increase kiCollisionLayerPairPreallocate in Collision.h", uiPairIndex, sLayerPairZones.size());
				DEBUG_BREAK();
				sLayerPairZones.resize(sLayerPairZones.empty() ? kiCollisionLayerPairPreallocate : static_cast<int64_t>(uiPairIndex) * 2);
			}
			LayerPairZones& rPairZones = sLayerPairZones.at(uiPairIndex);
			rPairZones.uiLayerA = static_cast<size_t>(iLayerA);
			rPairZones.uiLayerB = static_cast<size_t>(iLayerB);

			const CollisionLayer& rLayerA = sLayers.at(static_cast<size_t>(iLayerA));
			const CollisionLayer& rLayerB = sLayers.at(static_cast<size_t>(iLayerB));

			// Insert layer A objects
			for (int64_t i = 0; i < rLayerA.iCount; ++i)
			{
				if (rLayerA.pFlags[i] & kAlreadyCollided)
				{
					continue;
				}

				if (rLayerA.bSweptTest && rLayerA.pVecVelocities != nullptr)
				{
					XMVECTOR vecPos = rLayerA.pVecPositions[i];
					XMVECTOR vecEndPos = XMVectorAdd(vecPos, XMVectorScale(rLayerA.pVecVelocities[i], game::kfDeltaTime));
					InsertIntoZonesSwept(rPairZones, i, XMVectorMin(vecPos, vecEndPos), XMVectorMax(vecPos, vecEndPos), rLayerA.pfRadii[i], true);
				}
				else
				{
					InsertIntoZones(rPairZones, i, rLayerA.pVecPositions[i], rLayerA.pfRadii[i], true);
				}
			}

			// Insert layer B objects
			for (int64_t i = 0; i < rLayerB.iCount; ++i)
			{
				if (rLayerB.pFlags[i] & kAlreadyCollided)
				{
					continue;
				}

				if (rLayerB.bSweptTest && rLayerB.pVecVelocities != nullptr)
				{
					XMVECTOR vecPos = rLayerB.pVecPositions[i];
					XMVECTOR vecEndPos = XMVectorAdd(vecPos, XMVectorScale(rLayerB.pVecVelocities[i], game::kfDeltaTime));
					InsertIntoZonesSwept(rPairZones, i, XMVectorMin(vecPos, vecEndPos), XMVectorMax(vecPos, vecEndPos), rLayerB.pfRadii[i], false);
				}
				else
				{
					InsertIntoZones(rPairZones, i, rLayerB.pVecPositions[i], rLayerB.pfRadii[i], false);
				}
			}

			++uiPairIndex;
		}
	}
	siLayerPairCount = static_cast<int64_t>(uiPairIndex);
}

void Collision::Collide(const Alignments& rAlignments, FXMVECTOR vecArea)
{
	ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderCollide);

	// Clear previous frame results
	sResults.clear();

	// Build zone acceleration structure (includes layer pair filtering and same-layer collision assert)
	SetupZones(vecArea);

	// Process all layer pair zones
	for (int64_t i = 0; i < siLayerPairCount; ++i)
	{
		CollideLayerPair(rAlignments, sLayerPairZones.at(static_cast<size_t>(i)));
	}
}

void Collision::CollideLayerPair(const Alignments& rAlignments, LayerPairZones& rPairZones)
{
	size_t uiLayerA = rPairZones.uiLayerA;
	size_t uiLayerB = rPairZones.uiLayerB;

	CollisionLayer& rLayerA = sLayers.at(uiLayerA);
	CollisionLayer& rLayerB = sLayers.at(uiLayerB);

	bool bSweptPair = rLayerA.bSweptTest || rLayerB.bSweptTest;

	// Track tested B objects to avoid duplicates from multi-zone presence
	int64_t iTestedBSize = rLayerB.iCount * static_cast<int64_t>(sizeof(bool));
	bool* pTestedB = common::gpThreadLocal->mWorkbuffer.PushBuffer<bool*>(iTestedBSize);
	memset(pTestedB, 0, static_cast<size_t>(iTestedBSize));

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

		// Calculate A's zone range with clamping (expand to swept AABB if swept)
		float fMinX, fMaxX, fMinY, fMaxY;
		if (rLayerA.bSweptTest && rLayerA.pVecVelocities != nullptr)
		{
			XMVECTOR vecEndPos = XMVectorAdd(vecPositionA, XMVectorScale(rLayerA.pVecVelocities[i], game::kfDeltaTime));
			XMVECTOR vecMin = XMVectorMin(vecPositionA, vecEndPos);
			XMVECTOR vecMax = XMVectorMax(vecPositionA, vecEndPos);
			XMFLOAT4A f4Min {};
			XMFLOAT4A f4Max {};
			XMStoreFloat4A(&f4Min, vecMin);
			XMStoreFloat4A(&f4Max, vecMax);
			fMinX = f4Min.x;
			fMaxX = f4Max.x;
			fMinY = f4Min.y;
			fMaxY = f4Max.y;
		}
		else
		{
			XMFLOAT4A f4PositionA {};
			XMStoreFloat4A(&f4PositionA, vecPositionA);
			fMinX = f4PositionA.x;
			fMaxX = f4PositionA.x;
			fMinY = f4PositionA.y;
			fMaxY = f4PositionA.y;
		}

		int32_t iZoneStartX = std::clamp(static_cast<int32_t>((fMinX - fRadiusA - sfAreaMinX) / sfZoneWidth), 0, kiCollisionZonesX - 1);
		int32_t iZoneEndX = std::clamp(static_cast<int32_t>((fMaxX + fRadiusA - sfAreaMinX) / sfZoneWidth), 0, kiCollisionZonesX - 1);
		int32_t iZoneStartY = std::clamp(static_cast<int32_t>((fMinY - fRadiusA - sfAreaMinY) / sfZoneHeight), 0, kiCollisionZonesY - 1);
		int32_t iZoneEndY = std::clamp(static_cast<int32_t>((fMaxY + fRadiusA - sfAreaMinY) / sfZoneHeight), 0, kiCollisionZonesY - 1);

		// Clear tested flags for this A object
		memset(pTestedB, 0, static_cast<size_t>(iTestedBSize));

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
					if (pTestedB[j])
					{
						continue;
					}
					pTestedB[j] = true;

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

					XMVECTOR vecContactPoint = vecPositionA;

					// Swept sphere test (for fast-moving projectiles)
					if (bSweptPair)
					{
						XMVECTOR vecVelA = rLayerA.pVecVelocities != nullptr ? rLayerA.pVecVelocities[i] : XMVectorZero();
						XMVECTOR vecVelB = rLayerB.pVecVelocities != nullptr ? rLayerB.pVecVelocities[j] : XMVectorZero();
						float fContactTime = 0.0f;
						if (SweptSphereTest(vecPositionA, vecVelA, fRadiusA, vecPositionB, vecVelB, fRadiusB, fContactTime))
						{
							XMVECTOR vecImpactA = XMVectorAdd(vecPositionA, XMVectorScale(vecVelA, fContactTime));
							XMVECTOR vecImpactB = XMVectorAdd(vecPositionB, XMVectorScale(vecVelB, fContactTime));
							XMVECTOR vecDiff = XMVectorSubtract(vecImpactA, vecImpactB);
							float fDistance = XMVectorGetX(XMVector3Length(vecDiff));
							vecContactPoint = vecImpactA;
							if (fDistance > 0.0f)
							{
								vecContactPoint = XMVectorSubtract(vecImpactA, XMVectorScale(vecDiff, fRadiusA / fDistance));
							}
							goto record_collision;
						}
						// Fall through to discrete check (handles already-overlapping case)
					}

					// Discrete distance check
					{
						XMVECTOR vecDiff = XMVectorSubtract(vecPositionA, vecPositionB);
						float fDistanceSquared = XMVectorGetX(XMVector3LengthSq(vecDiff));
						float fCombinedRadius = fRadiusA + fRadiusB;

						if (fDistanceSquared > fCombinedRadius * fCombinedRadius)
						{
							continue;
						}

						float fDistance = std::sqrt(fDistanceSquared);
						if (fDistance > 0.0f)
						{
							XMVECTOR vecDirection = XMVectorScale(vecDiff, 1.0f / fDistance);
							vecContactPoint = XMVectorSubtract(vecPositionA, XMVectorScale(vecDirection, fRadiusA));
						}
					}

				record_collision:

					// Heap: unordered_map insert + vector push_back into sResults, which persists until PostCollision reads it.
					//   Can't use workbuffer (data outlives the call) or pre-allocate (collision count varies per frame)
					ScopedSuppressAllocationTracking suppressAllocationTracking;

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
						rLayerA.pFlags[i].Set(kAlreadyCollided);
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
						rLayerB.pFlags[j].Set(kAlreadyCollided);
					}
				}
			}
		}
	}

	common::gpThreadLocal->mWorkbuffer.Pop();
}

void Collision::Clear()
{
	siLayerCount = 0;
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
	int64_t iIndex = siAreaDamageSourceCount;
	if (siAreaDamageSourceCount >= static_cast<int64_t>(sAreaDamageSources.size()))
	{
		Log("Collision: sAreaDamageSources overflow (count: {}, capacity: {}). Increase kiAreaDamageSourcePreallocate in Collision.h", siAreaDamageSourceCount, sAreaDamageSources.size());
		DEBUG_BREAK();
		sAreaDamageSources.resize(sAreaDamageSources.empty() ? kiAreaDamageSourcePreallocate : siAreaDamageSourceCount * 2);
	}
	sAreaDamageSources.at(static_cast<size_t>(iIndex)) = rSource;
	++siAreaDamageSourceCount;
}

float Collision::GetAreaDamage(FXMVECTOR vecPosition, uint16_t uiCategoryMask, XMVECTOR& rvecClosestSource)
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

void Collision::ClearAreaDamage()
{
	siAreaDamageSourceCount = 0;
}

} // namespace engine
