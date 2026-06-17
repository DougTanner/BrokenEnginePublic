#include "Collision.h"

#include "Frame/Frame.h"
#include "Profile/ProfileManager.h"

namespace engine
{

// Pending collision result written to workbuffer during CollideLayerPair
struct PendingCollisionResult
{
	int64_t iLayerIndex = 0;
	int64_t iObjectIndex = 0;
	CollisionResult result;
};

// Zone range for spatial partitioning
struct ZoneRange
{
	int32_t iStartX = 0;
	int32_t iEndX = 0;
	int32_t iStartY = 0;
	int32_t iEndY = 0;
};

// Per-zone storage for a layer pair
struct ZonePair
{
	std::vector<int64_t> indicesA;  // Object indices from layer A
	std::vector<int64_t> indicesB;  // Object indices from layer B
	int64_t iCountA = 0;
	int64_t iCountB = 0;
};

// Grid of zones for one layer pair
struct LayerPairZones
{
	size_t uiLayerA = 0;
	size_t uiLayerB = 0;
	ZonePair zones[kiCollisionZonesY][kiCollisionZonesX];

	LayerPairZones()
	{
		for (ZonePair (&rRow)[kiCollisionZonesX] : zones)
		{
			for (ZonePair& rZonePair : rRow)
			{
				rZonePair.indicesA.resize(kiCollisionZonePreallocate);
				rZonePair.indicesB.resize(kiCollisionZonePreallocate);
			}
		}
	}
};

// thread_local definitions for Collision static members
thread_local float Collision::sfAreaMinX = 0.0f;
thread_local float Collision::sfAreaMinY = 0.0f;
thread_local float Collision::sfZoneWidth = 0.0f;
thread_local float Collision::sfZoneHeight = 0.0f;

// Default-constructed (no allocation): thread_local constructors run during
// mi_process_init before the allocator is ready, so pre-allocation would crash.
// The existing growth code handles lazy initialization on first use.
thread_local std::vector<CollisionLayer> Collision::sLayers;
thread_local int64_t Collision::siLayerCount = 0;

thread_local std::vector<LayerPairZones> Collision::sLayerPairZones;
thread_local int64_t Collision::siLayerPairCount = 0;

thread_local std::vector<CollisionResult> Collision::sResultEntries;
thread_local int64_t Collision::siResultEntryCount = 0;
thread_local std::vector<CollisionResultSpan> Collision::sResultSpans;
thread_local int64_t Collision::siResultSpanCount = 0;
thread_local int64_t Collision::sLayerBaseOffsets[kiCollisionLayerPreallocate] {};
thread_local std::vector<uint32_t> Collision::sTestedBGeneration;
thread_local uint32_t Collision::suiTestedBCurrentGeneration = 0;

using enum CollisionFlags;

size_t Collision::AddLayer(const CollisionLayer& rLayer)
{
	// Lazy pre-allocation: thread_local vectors start empty to avoid allocating
	// during mi_process_init (before the allocator is ready)
	if (sLayers.empty())
	{
		// Heap: one-time per-thread pre-allocation (thread_local vectors start empty to avoid allocating during mi_process_init)
		ScopedSuppressAllocationTracking suppress;
		sLayers.resize(kiCollisionLayerPreallocate);
	}

	size_t uiLayerIndex = static_cast<size_t>(siLayerCount);
	// Growing past the pre-allocation would overrun the fixed sLayerBaseOffsets array; layer count is
	// compile-time-determined by the registering collections, so overflow is a developer error — fail loud
	if (siLayerCount >= static_cast<int64_t>(sLayers.size()))
	{
		LOG(kDefault, kError, "Collision: sLayers overflow (count: {}, capacity: {}). Increase kiCollisionLayerPreallocate in Collision.h", siLayerCount, sLayers.size());
		ASSERT(false);
	}
	sLayers.at(uiLayerIndex) = rLayer;
	++siLayerCount;
	return uiLayerIndex;
}

ZoneRange Collision::CalculateZoneRange(float fMinX, float fMaxX, float fMinY, float fMaxY, float fRadius)
{
	return
	{
		.iStartX = std::clamp(static_cast<int32_t>((fMinX - fRadius - sfAreaMinX) / sfZoneWidth), 0, kiCollisionZonesX - 1),
		.iEndX = std::clamp(static_cast<int32_t>((fMaxX + fRadius - sfAreaMinX) / sfZoneWidth), 0, kiCollisionZonesX - 1),
		.iStartY = std::clamp(static_cast<int32_t>((fMinY - fRadius - sfAreaMinY) / sfZoneHeight), 0, kiCollisionZonesY - 1),
		.iEndY = std::clamp(static_cast<int32_t>((fMaxY + fRadius - sfAreaMinY) / sfZoneHeight), 0, kiCollisionZonesY - 1),
	};
}

ZoneRange Collision::CalculateObjectZoneRange(const CollisionLayer& rLayer, int64_t iIndex)
{
	float fMinX = 0.0f, fMaxX = 0.0f, fMinY = 0.0f, fMaxY = 0.0f;
	if (rLayer.bSweptTest && rLayer.pVecVelocities != nullptr)
	{
		XMVECTOR vecPos = rLayer.pVecPositions[iIndex];
		XMVECTOR vecEndPos = XMVectorAdd(vecPos, XMVectorScale(rLayer.pVecVelocities[iIndex], game::kfDeltaTime));
		XMVECTOR vecMin = XMVectorMin(vecPos, vecEndPos);
		XMVECTOR vecMax = XMVectorMax(vecPos, vecEndPos);
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
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, rLayer.pVecPositions[iIndex]);
		fMinX = f4Position.x;
		fMaxX = f4Position.x;
		fMinY = f4Position.y;
		fMaxY = f4Position.y;
	}
	return CalculateZoneRange(fMinX, fMaxX, fMinY, fMaxY, rLayer.pfRadii[iIndex]);
}

void Collision::InsertObjectIntoZones(LayerPairZones& rPairZones, int64_t iIndex, const ZoneRange& rRange, bool bIsLayerA)
{
	for (int32_t y = rRange.iStartY; y <= rRange.iEndY; ++y)
	{
		for (int32_t x = rRange.iStartX; x <= rRange.iEndX; ++x)
		{
			ZonePair& rZonePair = rPairZones.zones[y][x];
			const CollisionLayer& rLayerA = sLayers.at(rPairZones.uiLayerA);
			const CollisionLayer& rLayerB = sLayers.at(rPairZones.uiLayerB);
			if (bIsLayerA)
			{
				if (rZonePair.iCountA >= static_cast<int64_t>(rZonePair.indicesA.size()))
				{
					LOG(kDefault, kWarning, "Collision: ZonePair.indicesA overflow (count: {}, capacity: {}) pair A={}(cat={},total={}) B={}(cat={},total={}) zone=({},{}). Increase kiCollisionZonePreallocate in Collision.h", rZonePair.iCountA, rZonePair.indicesA.size(), rPairZones.uiLayerA, rLayerA.uiCategory, rLayerA.iCount, rPairZones.uiLayerB, rLayerB.uiCategory, rLayerB.iCount, x, y);
					DEBUG_BREAK();
					// Heap: rare growth when zone occupancy exceeds pre-allocation
					ScopedSuppressAllocationTracking suppress;
					rZonePair.indicesA.resize(rZonePair.iCountA * 2);
				}
				rZonePair.indicesA.at(static_cast<size_t>(rZonePair.iCountA)) = iIndex;
				++rZonePair.iCountA;
			}
			else
			{
				if (rZonePair.iCountB >= static_cast<int64_t>(rZonePair.indicesB.size()))
				{
					LOG(kDefault, kWarning, "Collision: ZonePair.indicesB overflow (count: {}, capacity: {}) pair A={}(cat={},total={}) B={}(cat={},total={}) zone=({},{}). Increase kiCollisionZonePreallocate in Collision.h", rZonePair.iCountB, rZonePair.indicesB.size(), rPairZones.uiLayerA, rLayerA.uiCategory, rLayerA.iCount, rPairZones.uiLayerB, rLayerB.uiCategory, rLayerB.iCount, x, y);
					DEBUG_BREAK();
					// Heap: rare growth when zone occupancy exceeds pre-allocation
					ScopedSuppressAllocationTracking suppress;
					rZonePair.indicesB.resize(rZonePair.iCountB * 2);
				}
				rZonePair.indicesB.at(static_cast<size_t>(rZonePair.iCountB)) = iIndex;
				++rZonePair.iCountB;
			}
		}
	}
}

static bool XM_CALLCONV SweptSphereTest(FXMVECTOR vecPosA, FXMVECTOR vecVelA, float fRadiusA, GXMVECTOR vecPosB, HXMVECTOR vecVelB, float fRadiusB, float& rfContactTime)
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

void Collision::SetupZones(FXMVECTOR vecArea)
{
	if (sLayerPairZones.empty())
	{
		// Heap: one-time per-thread pre-allocation (thread_local vectors start empty to avoid allocating during mi_process_init)
		ScopedSuppressAllocationTracking suppress;
		sLayerPairZones.resize(kiCollisionLayerPairPreallocate);
	}

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
				LOG(kDefault, kWarning, "Collision: sLayerPairZones overflow (index: {}, capacity: {}). Increase kiCollisionLayerPairPreallocate in Collision.h", uiPairIndex, sLayerPairZones.size());
				DEBUG_BREAK();
				// Heap: rare growth when layer-pair count exceeds pre-allocation (each new LayerPairZones pre-allocates its zone index vectors)
				ScopedSuppressAllocationTracking suppress;
				sLayerPairZones.resize(static_cast<int64_t>(uiPairIndex) * 2);
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

				ZoneRange range = CalculateObjectZoneRange(rLayerA, i);
				InsertObjectIntoZones(rPairZones, i, range, true);
			}

			// Insert layer B objects
			for (int64_t i = 0; i < rLayerB.iCount; ++i)
			{
				if (rLayerB.pFlags[i] & kAlreadyCollided)
				{
					continue;
				}

				ZoneRange range = CalculateObjectZoneRange(rLayerB, i);
				InsertObjectIntoZones(rPairZones, i, range, false);
			}

			++uiPairIndex;
		}
	}
	siLayerPairCount = static_cast<int64_t>(uiPairIndex);
}

void Collision::Collide(const Alignments& rAlignments, FXMVECTOR vecArea)
{
	ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderCollide);

	// Build zone acceleration structure (includes layer pair filtering and same-layer collision assert)
	SetupZones(vecArea);

	// Collect pending collision results into workbuffer
	common::ScopedWorkbufferArena scopedWorkbufferArena = common::gpThreadLocal->mWorkbuffer.Push();

	// Process all layer pair zones
	for (int64_t i = 0; i < siLayerPairCount; ++i)
	{
		CollideLayerPair(rAlignments, sLayerPairZones.at(static_cast<size_t>(i)));
	}

	// Bucket pending results into flat contiguous storage
	AllocateResultStorage();

	std::span<const PendingCollisionResult> pendingResults = common::gpThreadLocal->mWorkbuffer.Span<PendingCollisionResult>();

	// Pass 1: Count results per key
	for (const PendingCollisionResult& rPending : pendingResults)
	{
		int64_t iSpanIndex = sLayerBaseOffsets[static_cast<size_t>(rPending.iLayerIndex)] + rPending.iObjectIndex;
		++sResultSpans.at(static_cast<size_t>(iSpanIndex)).iCount;
	}

	// Prefix sum: assign offsets, reset counts for fill pass
	int64_t iTotalResults = 0;
	for (int64_t i = 0; i < siResultSpanCount; ++i)
	{
		CollisionResultSpan& rSpan = sResultSpans.at(static_cast<size_t>(i));
		if (rSpan.iCount > 0)
		{
			rSpan.iOffset = iTotalResults;
			iTotalResults += rSpan.iCount;
			rSpan.iCount = 0;
		}
	}

	// Grow result entries if needed
	if (iTotalResults > 0)
	{
		if (sResultEntries.empty())
		{
			// Heap: one-time per-thread pre-allocation (thread_local vectors start empty to avoid allocating during mi_process_init)
			ScopedSuppressAllocationTracking suppress;
			sResultEntries.resize(std::max(kiCollisionResultPreallocate, iTotalResults));
		}
		else if (iTotalResults > static_cast<int64_t>(sResultEntries.size()))
		{
			// Heap: rare growth when collision count exceeds pre-allocation
			ScopedSuppressAllocationTracking suppress;
			sResultEntries.resize(iTotalResults);
		}
	}
	siResultEntryCount = iTotalResults;

	// Pass 2: Fill results at their assigned offsets
	for (const PendingCollisionResult& rPending : pendingResults)
	{
		int64_t iSpanIndex = sLayerBaseOffsets[static_cast<size_t>(rPending.iLayerIndex)] + rPending.iObjectIndex;
		CollisionResultSpan& rSpan = sResultSpans.at(static_cast<size_t>(iSpanIndex));
		sResultEntries.at(static_cast<size_t>(rSpan.iOffset + rSpan.iCount)) = rPending.result;
		++rSpan.iCount;
	}

}

void Collision::AllocateResultStorage()
{
	// Compute layer base offsets (prefix sum of layer counts)
	int64_t iTotal = 0;
	for (int64_t i = 0; i < siLayerCount; ++i)
	{
		sLayerBaseOffsets[static_cast<size_t>(i)] = iTotal;
		iTotal += sLayers.at(static_cast<size_t>(i)).iCount;
	}
	siResultSpanCount = iTotal;

	// Lazy pre-allocate spans
	if (sResultSpans.empty())
	{
		// Heap: one-time per-thread pre-allocation (thread_local vectors start empty to avoid allocating during mi_process_init)
		ScopedSuppressAllocationTracking suppress;
		sResultSpans.resize(std::max(kiCollisionResultSpanPreallocate, iTotal));
	}
	else if (iTotal > static_cast<int64_t>(sResultSpans.size()))
	{
		// Heap: rare growth when total object count exceeds pre-allocation
		ScopedSuppressAllocationTracking suppress;
		sResultSpans.resize(iTotal);
	}

	// Reset all active spans
	for (int64_t i = 0; i < iTotal; ++i)
	{
		sResultSpans.at(static_cast<size_t>(i)) = {-1, 0};
	}
}

void Collision::CollideLayerPair(const Alignments& rAlignments, LayerPairZones& rPairZones)
{
	size_t uiLayerA = rPairZones.uiLayerA;
	size_t uiLayerB = rPairZones.uiLayerB;

	CollisionLayer& rLayerA = sLayers.at(uiLayerA);
	CollisionLayer& rLayerB = sLayers.at(uiLayerB);

	bool bSweptPair = rLayerA.bSweptTest || rLayerB.bSweptTest;

	// Track tested B objects to avoid duplicates from multi-zone presence (generation counter)
	if (rLayerB.iCount > static_cast<int64_t>(sTestedBGeneration.size()))
	{
		// Heap: one-time per-thread growth (thread_local vectors start empty to avoid allocating during mi_process_init)
		ScopedSuppressAllocationTracking suppress;
		sTestedBGeneration.resize(static_cast<size_t>(rLayerB.iCount));
		std::memset(sTestedBGeneration.data(), 0, sTestedBGeneration.size() * sizeof(uint32_t));
	}

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
		ZoneRange range = CalculateObjectZoneRange(rLayerA, i);

		// Increment generation counter instead of memset per A object
		++suiTestedBCurrentGeneration;
		if (suiTestedBCurrentGeneration == 0)
		{
			std::memset(sTestedBGeneration.data(), 0, sTestedBGeneration.size() * sizeof(uint32_t));
			suiTestedBCurrentGeneration = 1;
		}

		// Iterate zones in A's range
		for (int32_t y = range.iStartY; y <= range.iEndY; ++y)
		{
			for (int32_t x = range.iStartX; x <= range.iEndX; ++x)
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
					if (sTestedBGeneration[j] == suiTestedBCurrentGeneration)
					{
						continue;
					}
					sTestedBGeneration[j] = suiTestedBCurrentGeneration;

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
					bool bCollided = false;

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
							bCollided = true;
						}
						// Fall through to discrete check (handles already-overlapping case)
					}

					// Discrete distance check
					if (!bCollided)
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

					// Record collision for A (always bidirectional per assert in SetupZones)
					float fDamageB = rLayerB.pfDamages[j];

					common::gpThreadLocal->mWorkbuffer.PushBack<PendingCollisionResult>(
					{
						.iLayerIndex = static_cast<int64_t>(uiLayerA),
						.iObjectIndex = i,
						.result =
						{
							.iOtherIndex = j,
							.uiOtherLayerIndex = uiLayerB,
							.uiOtherCategory = rLayerB.uiCategory,
							.fDamageReceived = fDamageB,
							.vecContactPoint = vecContactPoint,
							.vecOtherVelocity = rLayerB.pVecVelocities != nullptr ? rLayerB.pVecVelocities[j] : XMVectorZero(),
						},
					});

					// Mark A as already collided if it's destroy-on-collide
					if (rLayerA.pFlags[i] & kDestroyOnCollide)
					{
						rLayerA.pFlags[i].Set(kAlreadyCollided);
					}

					// Record collision for B (always bidirectional per assert in SetupZones)
					float fDamageA = rLayerA.pfDamages[i];

					common::gpThreadLocal->mWorkbuffer.PushBack<PendingCollisionResult>(
					{
						.iLayerIndex = static_cast<int64_t>(uiLayerB),
						.iObjectIndex = j,
						.result =
						{
							.iOtherIndex = i,
							.uiOtherLayerIndex = uiLayerA,
							.uiOtherCategory = rLayerA.uiCategory,
							.fDamageReceived = fDamageA,
							.vecContactPoint = vecContactPoint,
							.vecOtherVelocity = rLayerA.pVecVelocities != nullptr ? rLayerA.pVecVelocities[i] : XMVectorZero(),
						},
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
}

void Collision::Clear()
{
	siLayerCount = 0;
}

bool Collision::HasCollision(size_t uiLayerIndex, int64_t iIndex)
{
	return sResultSpans.at(static_cast<size_t>(sLayerBaseOffsets[uiLayerIndex] + iIndex)).iCount > 0;
}

std::span<const CollisionResult> Collision::GetCollisions(size_t uiLayerIndex, int64_t iIndex)
{
	const CollisionResultSpan& rSpan = sResultSpans.at(static_cast<size_t>(sLayerBaseOffsets[uiLayerIndex] + iIndex));
	if (rSpan.iCount > 0)
	{
		return {sResultEntries.data() + rSpan.iOffset, static_cast<size_t>(rSpan.iCount)};
	}
	return {};
}

} // namespace engine
