#include "Collision.h"

#include "Frame/Frame.h"
#include "Profile/ProfileManager.h"

namespace engine
{

// Accepted collision result retained after globally sorting candidate events.
struct PendingCollisionResult
{
	int64_t iLayerIndex = 0;
	int64_t iObjectIndex = 0;
	CollisionResult result;
};

// Candidate event collected across every active layer pair before any collision flags mutate.
struct CollisionCandidate
{
	float fTimeOfImpact = 0.0f;
	size_t uiLayerA = 0;
	int64_t iObjectA = 0;
	size_t uiLayerB = 0;
	int64_t iObjectB = 0;
	XMVECTOR vecContactPoint {};
	XMVECTOR vecPositionA {};
	XMVECTOR vecPositionB {};
};

// Values shared by every object pair generated for one layer pair.
struct CollisionPairContext
{
	const Alignments& rAlignments;
	const CollisionLayer& rLayerA;
	const CollisionLayer& rLayerB;
	size_t uiLayerA = 0;
	size_t uiLayerB = 0;
	bool bSweptPair = false;
};

struct CollisionEventScratch
{
	std::vector<CollisionCandidate> candidates;
	std::vector<PendingCollisionResult> pendingResults;
	int64_t iCandidateCount = 0;
	int64_t iPendingResultCount = 0;
};

static CollisionEventScratch& GetCollisionEventScratch()
{
	// Function-local TLS defers construction until first use; default construction is allocation-free
	// (empty vectors), so it is safe even before allocator startup completes. Growth sites suppress tracking.
	static thread_local CollisionEventScratch sScratch;
	return sScratch;
}

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

ZoneRange Collision::CalculateObjectZoneRange(const CollisionLayer& rLayer, int64_t iIndex, bool bSweptPair)
{
	float fMinX = 0.0f, fMaxX = 0.0f, fMinY = 0.0f, fMaxY = 0.0f;
	if (bSweptPair)
	{
		XMVECTOR vecMin = XMVectorMin(rLayer.pVecStartPositions[iIndex], rLayer.pVecEndPositions[iIndex]);
		XMVECTOR vecMax = XMVectorMax(rLayer.pVecStartPositions[iIndex], rLayer.pVecEndPositions[iIndex]);
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
		XMStoreFloat4A(&f4Position, rLayer.pVecEndPositions[iIndex]);
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
			std::vector<int64_t>& rIndices = bIsLayerA ? rZonePair.indicesA : rZonePair.indicesB;
			int64_t& riCount = bIsLayerA ? rZonePair.iCountA : rZonePair.iCountB;
			if (riCount >= static_cast<int64_t>(rIndices.size()))
			{
				// sLayers lookups feed only this rare overflow LOG, so fetch them here, not every zone iteration
				const CollisionLayer& rLayerA = sLayers.at(rPairZones.uiLayerA);
				const CollisionLayer& rLayerB = sLayers.at(rPairZones.uiLayerB);
				LOG(kDefault, kWarning, "Collision: ZonePair.indices{} overflow (count: {}, capacity: {}) pair A={}(cat={},total={}) B={}(cat={},total={}) zone=({},{}). Increase kiCollisionZonePreallocate in Collision.h", bIsLayerA ? 'A' : 'B', riCount, rIndices.size(), rPairZones.uiLayerA, rLayerA.uiCategory, rLayerA.iCount, rPairZones.uiLayerB, rLayerB.uiCategory, rLayerB.iCount, x, y);
				DEBUG_BREAK();
				// Heap: rare growth when zone occupancy exceeds pre-allocation
				ScopedSuppressAllocationTracking suppress;
				rIndices.resize(riCount * 2);
			}
			rIndices.at(static_cast<size_t>(riCount)) = iIndex;
			++riCount;
		}
	}
}

void Collision::InsertLayerObjectsIntoZones(LayerPairZones& rPairZones, const CollisionLayer& rLayer, bool bIsLayerA, bool bSweptPair)
{
	for (int64_t i = 0; i < rLayer.iCount; ++i)
	{
		if (rLayer.pFlags[i] & kAlreadyCollided)
		{
			continue;
		}

		ZoneRange range = CalculateObjectZoneRange(rLayer, i, bSweptPair);
		InsertObjectIntoZones(rPairZones, i, range, bIsLayerA);
	}
}

static XMVECTOR XM_CALLCONV PositionAtTime(const CollisionLayer& rLayer, int64_t iIndex, float fTime)
{
	float fStartTime = rLayer.pfStartTimes[iIndex];
	float fEndTime = rLayer.pfEndTimes[iIndex];
	if (fEndTime <= fStartTime)
	{
		return rLayer.pVecStartPositions[iIndex];
	}

	float fPercent = (fTime - fStartTime) / (fEndTime - fStartTime);
	return XMVectorLerp(rLayer.pVecStartPositions[iIndex], rLayer.pVecEndPositions[iIndex], fPercent);
}

static bool XM_CALLCONV SweptSphereTest(FXMVECTOR vecStartA, FXMVECTOR vecEndA, float fRadiusA, FXMVECTOR vecStartB, GXMVECTOR vecEndB, float fRadiusB, float fStartTime, float fEndTime, float& rfTimeOfImpact)
{
	XMVECTOR vecRelativeStart = XMVectorSubtract(vecStartB, vecStartA);
	XMVECTOR vecRelativeDelta = XMVectorSubtract(XMVectorSubtract(vecEndB, vecStartB), XMVectorSubtract(vecEndA, vecStartA));
	float fCombinedRadius = fRadiusA + fRadiusB;

	// Quadratic coefficients for swept intersection
	float fQuadraticConstant = XMVectorGetX(XMVector3Dot(vecRelativeStart, vecRelativeStart)) - fCombinedRadius * fCombinedRadius;
	if (fQuadraticConstant <= 0.0f)
	{
		rfTimeOfImpact = fStartTime;
		return true;
	}

	float fQuadraticLinear = 2.0f * XMVectorGetX(XMVector3Dot(vecRelativeStart, vecRelativeDelta));
	if (fQuadraticLinear >= 0.0f)
	{
		// Moving apart
		return false;
	}

	float fQuadraticSquared = XMVectorGetX(XMVector3Dot(vecRelativeDelta, vecRelativeDelta));
	if (fQuadraticSquared < 1.0e-8f)
	{
		// No relative motion
		return false;
	}

	float fDiscriminant = fQuadraticLinear * fQuadraticLinear - 4.0f * fQuadraticSquared * fQuadraticConstant;
	if (fDiscriminant < 0.0f)
	{
		return false;
	}

	// Earliest contact time within the common absolute tick interval
	float fNormalizedTime = (-fQuadraticLinear - std::sqrt(fDiscriminant)) / (2.0f * fQuadraticSquared);
	if (fNormalizedTime >= 0.0f && fNormalizedTime <= 1.0f)
	{
		rfTimeOfImpact = fStartTime + fNormalizedTime * (fEndTime - fStartTime);
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
			bool bSweptPair = rLayerA.bSweptTest || rLayerB.bSweptTest;

			// Insert both layers' objects into the zone grid (skip already-collided destroy-on-collide objects)
			InsertLayerObjectsIntoZones(rPairZones, rLayerA, true, bSweptPair);
			InsertLayerObjectsIntoZones(rPairZones, rLayerB, false, bSweptPair);

			++uiPairIndex;
		}
	}
	siLayerPairCount = static_cast<int64_t>(uiPairIndex);
}

void Collision::Collide(const Alignments& rAlignments, FXMVECTOR vecArea)
{
	CollisionEventScratch& rScratch = GetCollisionEventScratch();
	ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderCollide);

	// Build zone acceleration structure (includes layer pair filtering and same-layer collision assert)
	SetupZones(vecArea);

	// Collect every candidate before mutating collision flags so traversal order cannot select winners.
	if (rScratch.candidates.empty())
	{
		// Heap: one-time per-thread pre-allocation; retained for later ticks.
		ScopedSuppressAllocationTracking suppress;
		rScratch.candidates.resize(kiCollisionCandidatePreallocate);
	}
	rScratch.iCandidateCount = 0;
	if (rScratch.pendingResults.empty())
	{
		// Heap: one-time per-thread pre-allocation; retained for later ticks.
		ScopedSuppressAllocationTracking suppress;
		rScratch.pendingResults.resize(kiCollisionResultPreallocate);
	}
	rScratch.iPendingResultCount = 0;

	// Process all layer pair zones
	for (int64_t i = 0; i < siLayerPairCount; ++i)
	{
		CollideLayerPair(rAlignments, sLayerPairZones.at(static_cast<size_t>(i)));
	}

	// One global sort, not per layer pair. The trailing layer/object keys are load-bearing: for the fixed
	// layer-registration order client and server share, they make the equal-time order total and reproducible.
	// Sorting by fTimeOfImpact alone leaves equal-time candidates in an arbitrary order and desyncs client and server.
	std::sort(rScratch.candidates.begin(), rScratch.candidates.begin() + rScratch.iCandidateCount, [](const CollisionCandidate& rLeftCandidate, const CollisionCandidate& rRightCandidate)
	{
		return std::tie(rLeftCandidate.fTimeOfImpact, rLeftCandidate.uiLayerA, rLeftCandidate.iObjectA, rLeftCandidate.uiLayerB, rLeftCandidate.iObjectB) <
		       std::tie(rRightCandidate.fTimeOfImpact, rRightCandidate.uiLayerA, rRightCandidate.iObjectA, rRightCandidate.uiLayerB, rRightCandidate.iObjectB);
	});

	// Commit accepted candidates into retained pending-result scratch.
	for (int64_t i = 0; i < rScratch.iCandidateCount; ++i)
	{
		CommitCandidate(rScratch.candidates.at(static_cast<size_t>(i)));
	}

	// Bucket pending results into flat contiguous storage
	AllocateResultStorage();

	// Pass 1: Count results per key
	for (int64_t i = 0; i < rScratch.iPendingResultCount; ++i)
	{
		const PendingCollisionResult& rPending = rScratch.pendingResults.at(static_cast<size_t>(i));
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
	// Pass 2: Fill results at their assigned offsets
	for (int64_t i = 0; i < rScratch.iPendingResultCount; ++i)
	{
		const PendingCollisionResult& rPending = rScratch.pendingResults.at(static_cast<size_t>(i));
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
		sResultSpans.at(static_cast<size_t>(i)) = {.iOffset = -1, .iCount = 0};
	}
}

// Record one side of a bidirectional collision: self receives the other side's damage and velocity.
static void XM_CALLCONV RecordCollision(const CollisionCandidate& rCandidate, FXMVECTOR vecSelfPosition, int64_t iSelf, size_t uiSelfLayer, CollisionLayer& rOtherLayer, int64_t iOther, size_t uiOtherLayer)
{
	CollisionEventScratch& rScratch = GetCollisionEventScratch();
	if (rScratch.iPendingResultCount >= static_cast<int64_t>(rScratch.pendingResults.size())) [[unlikely]]
	{
		LOG(kDefault, kWarning, "Collision: pendingResults overflow (count: {}, capacity: {}). Increase kiCollisionResultPreallocate in Collision.h", rScratch.iPendingResultCount, rScratch.pendingResults.size());
		DEBUG_BREAK();
		// Heap: rare growth when accepted result count exceeds pre-allocation.
		ScopedSuppressAllocationTracking suppress;
		rScratch.pendingResults.resize(rScratch.iPendingResultCount * 2);
	}
	rScratch.pendingResults.at(static_cast<size_t>(rScratch.iPendingResultCount)) =
	{
		.iLayerIndex = static_cast<int64_t>(uiSelfLayer),
		.iObjectIndex = iSelf,
		.result =
		{
			.iOtherIndex = iOther,
			.uiOtherLayerIndex = uiOtherLayer,
			.uiOtherCategory = rOtherLayer.uiCategory,
			.fDamageReceived = rOtherLayer.pfDamages[iOther],
			.fTimeOfImpact = rCandidate.fTimeOfImpact,
			.vecContactPoint = rCandidate.vecContactPoint,
			.vecSelfPosition = vecSelfPosition,
			.vecOtherVelocity = rOtherLayer.pVecVelocities != nullptr ? rOtherLayer.pVecVelocities[iOther] : XMVectorZero(),
		},
	};
	++rScratch.iPendingResultCount;
}

void Collision::CommitCandidate(const CollisionCandidate& rCandidate)
{
	CollisionLayer& rLayerA = Collision::sLayers.at(rCandidate.uiLayerA);
	CollisionLayer& rLayerB = Collision::sLayers.at(rCandidate.uiLayerB);
	if ((rLayerA.pFlags[rCandidate.iObjectA] & kAlreadyCollided) ||
		(rLayerB.pFlags[rCandidate.iObjectB] & kAlreadyCollided))
	{
		return;
	}

	RecordCollision(rCandidate, rCandidate.vecPositionA, rCandidate.iObjectA, rCandidate.uiLayerA, rLayerB, rCandidate.iObjectB, rCandidate.uiLayerB);
	RecordCollision(rCandidate, rCandidate.vecPositionB, rCandidate.iObjectB, rCandidate.uiLayerB, rLayerA, rCandidate.iObjectA, rCandidate.uiLayerA);

	// Mutate flags only after both result sides are committed.
	if (rLayerA.pFlags[rCandidate.iObjectA] & kDestroyOnCollide)
	{
		rLayerA.pFlags[rCandidate.iObjectA].Set(kAlreadyCollided);
	}
	if (rLayerB.pFlags[rCandidate.iObjectB] & kDestroyOnCollide)
	{
		rLayerB.pFlags[rCandidate.iObjectB].Set(kAlreadyCollided);
	}
}

// Test one A object against one B object and collect a globally sortable event. The caller handles
// the per-B tested-this-A-object dedup.
static void TestAndCollectPair(const CollisionPairContext& rPairContext, int64_t i, int64_t j)
{
	const Alignments& rAlignments = rPairContext.rAlignments;
	const CollisionLayer& rLayerA = rPairContext.rLayerA;
	const CollisionLayer& rLayerB = rPairContext.rLayerB;
	size_t uiLayerA = rPairContext.uiLayerA;
	size_t uiLayerB = rPairContext.uiLayerB;
	bool bSweptPair = rPairContext.bSweptPair;

	// Skip if alignments don't allow collision
	if (!rAlignments.CanCollide(rLayerA.pAlignments[i], rLayerB.pAlignments[j]))
	{
		return;
	}

	// Pre-existing inactive entries never generate candidates. Newly accepted destroy collisions are
	// resolved later after the global sort.
	if ((rLayerA.pFlags[i] & kAlreadyCollided) || (rLayerB.pFlags[j] & kAlreadyCollided))
	{
		return;
	}

	float fStartTime = std::max(rLayerA.pfStartTimes[i], rLayerB.pfStartTimes[j]);
	float fEndTime = std::min(rLayerA.pfEndTimes[i], rLayerB.pfEndTimes[j]);
	if (fStartTime > fEndTime)
	{
		return;
	}

	XMVECTOR vecStartA = PositionAtTime(rLayerA, i, fStartTime);
	XMVECTOR vecEndA = PositionAtTime(rLayerA, i, fEndTime);
	XMVECTOR vecStartB = PositionAtTime(rLayerB, j, fStartTime);
	XMVECTOR vecEndB = PositionAtTime(rLayerB, j, fEndTime);
	float fRadiusA = rLayerA.pfRadii[i];
	float fRadiusB = rLayerB.pfRadii[j];

	bool bCollided = false;
	float fTimeOfImpact = fEndTime;
	XMVECTOR vecImpactA = vecEndA;
	XMVECTOR vecImpactB = vecEndB;

	if (bSweptPair)
	{
		if (SweptSphereTest(vecStartA, vecEndA, fRadiusA, vecStartB, vecEndB, fRadiusB, fStartTime, fEndTime, fTimeOfImpact))
		{
			vecImpactA = PositionAtTime(rLayerA, i, fTimeOfImpact);
			vecImpactB = PositionAtTime(rLayerB, j, fTimeOfImpact);
			bCollided = true;
		}
	}

	if (!bCollided)
	{
		XMVECTOR vecDifference = XMVectorSubtract(vecImpactA, vecImpactB);
		float fDistanceSquared = XMVectorGetX(XMVector3LengthSq(vecDifference));
		float fCombinedRadius = fRadiusA + fRadiusB;

		if (fDistanceSquared > fCombinedRadius * fCombinedRadius)
		{
			return;
		}

	}

	float fMaxTimeA = rLayerA.pfMaxTimes != nullptr ? rLayerA.pfMaxTimes[i] : std::numeric_limits<float>::max();
	float fMaxTimeB = rLayerB.pfMaxTimes != nullptr ? rLayerB.pfMaxTimes[j] : std::numeric_limits<float>::max();
	if (fTimeOfImpact >= fMaxTimeA || fTimeOfImpact >= fMaxTimeB)
	{
		return;
	}

	XMVECTOR vecDifference = XMVectorSubtract(vecImpactA, vecImpactB);
	float fDistance = XMVectorGetX(XMVector3Length(vecDifference));
	XMVECTOR vecContactPoint = vecImpactA;
	if (fDistance > 0.0f)
	{
		vecContactPoint = XMVectorSubtract(vecImpactA, XMVectorScale(vecDifference, fRadiusA / fDistance));
	}

	CollisionEventScratch& rScratch = GetCollisionEventScratch();
	if (rScratch.iCandidateCount >= static_cast<int64_t>(rScratch.candidates.size())) [[unlikely]]
	{
		LOG(kDefault, kWarning, "Collision: candidates overflow (count: {}, capacity: {}). Increase kiCollisionCandidatePreallocate in Collision.h", rScratch.iCandidateCount, rScratch.candidates.size());
		DEBUG_BREAK();
		// Heap: rare growth when candidate count exceeds pre-allocation.
		ScopedSuppressAllocationTracking suppress;
		rScratch.candidates.resize(rScratch.iCandidateCount * 2);
	}
	rScratch.candidates.at(static_cast<size_t>(rScratch.iCandidateCount)) =
	{
		.fTimeOfImpact = fTimeOfImpact,
		.uiLayerA = uiLayerA,
		.iObjectA = i,
		.uiLayerB = uiLayerB,
		.iObjectB = j,
		.vecContactPoint = vecContactPoint,
		.vecPositionA = vecImpactA,
		.vecPositionB = vecImpactB,
	};
	++rScratch.iCandidateCount;
}

void Collision::CollideLayerPair(const Alignments& rAlignments, LayerPairZones& rPairZones)
{
	size_t uiLayerA = rPairZones.uiLayerA;
	size_t uiLayerB = rPairZones.uiLayerB;

	CollisionLayer& rLayerA = sLayers.at(uiLayerA);
	CollisionLayer& rLayerB = sLayers.at(uiLayerB);

	bool bSweptPair = rLayerA.bSweptTest || rLayerB.bSweptTest;
	CollisionPairContext pairContext
	{
		.rAlignments = rAlignments,
		.rLayerA = rLayerA,
		.rLayerB = rLayerB,
		.uiLayerA = uiLayerA,
		.uiLayerB = uiLayerB,
		.bSweptPair = bSweptPair,
	};

	// Track tested B objects to avoid duplicates from multi-zone presence (generation counter)
	if (rLayerB.iCount > static_cast<int64_t>(sTestedBGeneration.size()))
	{
		// Heap: one-time per-thread growth (thread_local vectors start empty to avoid allocating during mi_process_init)
		ScopedSuppressAllocationTracking suppress;
		sTestedBGeneration.resize(static_cast<size_t>(rLayerB.iCount));
	}

	for (int64_t i = 0; i < rLayerA.iCount; ++i)
	{
		// Skip if already collided this frame (for destroy-on-collide objects)
		if (rLayerA.pFlags[i] & kAlreadyCollided)
		{
			continue;
		}

		// Calculate A's zone range with clamping (expand to swept AABB if swept)
		ZoneRange range = CalculateObjectZoneRange(rLayerA, i, bSweptPair);

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
					if (sTestedBGeneration.at(static_cast<size_t>(j)) == suiTestedBCurrentGeneration)
					{
						continue;
					}
					sTestedBGeneration.at(static_cast<size_t>(j)) = suiTestedBCurrentGeneration;

					TestAndCollectPair(pairContext, i, j);
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
