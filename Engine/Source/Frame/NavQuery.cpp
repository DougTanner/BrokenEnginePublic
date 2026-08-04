#include "NavQuery.h"

#include "NavBuild.h"
#include "NavBuildInternal.h"
#include "Ui/WrapperBase.h"

namespace engine
{

// Test if a segment intersects any obstacle edge, using the NavData edge grid as a broad phase. The
// result is an order-independent boolean OR, so grid-traversal order never changes it -> deterministic.
// Precondition: rNavData carries the gridMin/gridMax and edge CSR that BuildNavAcceleration derives from
// a non-empty vertex set (the CSR is empty otherwise). Each caller establishes that for itself — the
// query path through NavQueryDirection's empty-vertices early-out, the build path in NavCellData.cpp by
// running the visibility pass after BuildNavAcceleration and only on a non-empty cell.
// Soundness: edges are bucketed into every cell their AABB overlaps (conservative), and the DDA walks
// every cell the clipped segment passes through, so a real crossing is always found.
// Promoted from the anonymous namespace to external linkage (engine::, declared in NavBuildInternal.h)
// so the cell visibility build and the runtime query can never disagree about what is blocked.
bool SegmentBlockedByObstacle(XMFLOAT2 f2A, XMFLOAT2 f2B, const XMFLOAT2* pVertices, const NavData& rNavData)
{
	float fMinX = rNavData.gridMin.x;
	float fMinY = rNavData.gridMin.y;
	float fMaxX = rNavData.gridMax.x;
	float fMaxY = rNavData.gridMax.y;

	float fDx = f2B.x - f2A.x;
	float fDy = f2B.y - f2A.y;

	// Obstacle edges live strictly inside the grid domain, so an intersection can only occur there.
	// Slab-clip the query segment to [min,max] (parametric range [fT0,fT1] over A->B).
	float fT0 = 0.0f;
	float fT1 = 1.0f;
	if (std::abs(fDx) < 1e-20f)
	{
		if (f2A.x < fMinX || f2A.x > fMaxX)
		{
			return false;
		}
	}
	else
	{
		float fInv = 1.0f / fDx;
		float fTa = (fMinX - f2A.x) * fInv;
		float fTb = (fMaxX - f2A.x) * fInv;
		if (fTa > fTb)
		{
			float fTmp = fTa;
			fTa = fTb;
			fTb = fTmp;
		}
		fT0 = std::max(fT0, fTa);
		fT1 = std::min(fT1, fTb);
	}
	if (std::abs(fDy) < 1e-20f)
	{
		if (f2A.y < fMinY || f2A.y > fMaxY)
		{
			return false;
		}
	}
	else
	{
		float fInv = 1.0f / fDy;
		float fTa = (fMinY - f2A.y) * fInv;
		float fTb = (fMaxY - f2A.y) * fInv;
		if (fTa > fTb)
		{
			float fTmp = fTa;
			fTa = fTb;
			fTb = fTmp;
		}
		fT0 = std::max(fT0, fTa);
		fT1 = std::min(fT1, fTb);
	}
	if (fT0 > fT1)
	{
		return false;
	}

	// Clipped endpoints (both inside the grid domain).
	float fP0x = f2A.x + fT0 * fDx;
	float fP0y = f2A.y + fT0 * fDy;
	float fP1x = f2A.x + fT1 * fDx;
	float fP1y = f2A.y + fT1 * fDy;

	float fCellSizeX = (fMaxX - fMinX) / static_cast<float>(kiNavZonesX);
	float fCellSizeY = (fMaxY - fMinY) / static_cast<float>(kiNavZonesY);

	int32_t iX = NavGridCell(fP0x, fMinX, fMaxX, kiNavZonesX);
	int32_t iY = NavGridCell(fP0y, fMinY, fMaxY, kiNavZonesY);
	int32_t iEndX = NavGridCell(fP1x, fMinX, fMaxX, kiNavZonesX);
	int32_t iEndY = NavGridCell(fP1y, fMinY, fMaxY, kiNavZonesY);

	float fSegDx = fP1x - fP0x;
	float fSegDy = fP1y - fP0y;
	int32_t iStepX = (fSegDx > 0.0f) ? 1 : ((fSegDx < 0.0f) ? -1 : 0);
	int32_t iStepY = (fSegDy > 0.0f) ? 1 : ((fSegDy < 0.0f) ? -1 : 0);

	// Amanatides-Woo parametric crossing distances (in segment-t units).
	float fTMaxX = std::numeric_limits<float>::max();
	float fTDeltaX = std::numeric_limits<float>::max();
	if (iStepX != 0 && fCellSizeX > 1e-6f)
	{
		float fBoundaryX = fMinX + static_cast<float>(iX + (iStepX > 0 ? 1 : 0)) * fCellSizeX;
		fTMaxX = (fBoundaryX - fP0x) / fSegDx;
		fTDeltaX = fCellSizeX / std::abs(fSegDx);
	}
	float fTMaxY = std::numeric_limits<float>::max();
	float fTDeltaY = std::numeric_limits<float>::max();
	if (iStepY != 0 && fCellSizeY > 1e-6f)
	{
		float fBoundaryY = fMinY + static_cast<float>(iY + (iStepY > 0 ? 1 : 0)) * fCellSizeY;
		fTMaxY = (fBoundaryY - fP0y) / fSegDy;
		fTDeltaY = fCellSizeY / std::abs(fSegDy);
	}

	// Walk cells along the clipped segment. Bounded by the grid extent; the step guard prevents any
	// runaway from float drift.
	int32_t iMaxSteps = 2 * (kiNavZonesX + kiNavZonesY);
	for (int32_t iStep = 0; iStep <= iMaxSteps; ++iStep)
	{
		int32_t iCell = iY * kiNavZonesX + iX;
		int32_t iBegin = rNavData.gridEdgeOffsets.at(static_cast<size_t>(iCell));
		int32_t iStop = rNavData.gridEdgeOffsets.at(static_cast<size_t>(iCell) + 1);
		for (int32_t k = iBegin; k < iStop; ++k)
		{
			int32_t iEdge = rNavData.gridEdges.at(static_cast<size_t>(k));
			if (SegmentsIntersect(f2A, f2B, pVertices[rNavData.edgeA.at(static_cast<size_t>(iEdge))], pVertices[rNavData.edgeB.at(static_cast<size_t>(iEdge))]))
			{
				return true;
			}
		}

		if (iX == iEndX && iY == iEndY)
		{
			break;
		}

		if (fTMaxX < fTMaxY)
		{
			iX += iStepX;
			fTMaxX += fTDeltaX;
		}
		else
		{
			iY += iStepY;
			fTMaxY += fTDeltaY;
		}

		if (iX < 0 || iX >= kiNavZonesX || iY < 0 || iY >= kiNavZonesY)
		{
			break;
		}
	}
	return false;
}

// Is the point inside any obstacle polygon? AABB broad phase per polygon, then the shared winding-
// number PointInPolygon core (single-sourced with the builder via NavBuildInternal.h). Promoted to
// external linkage alongside SegmentBlockedByObstacle above, for the same reason.
bool PointInAnyPolygon(XMFLOAT2 f2Point, const XMFLOAT2* pVertices, const NavData& rNavData)
{
	for (size_t iPoly = 0; iPoly < rNavData.polygonOffsets.size(); ++iPoly)
	{
		// Broad phase: a point outside the polygon's AABB cannot be inside the polygon.
		const XMFLOAT2& rMin = rNavData.polygonMin.at(iPoly);
		const XMFLOAT2& rMax = rNavData.polygonMax.at(iPoly);
		if (f2Point.x < rMin.x || f2Point.x > rMax.x || f2Point.y < rMin.y || f2Point.y > rMax.y)
		{
			continue;
		}

		auto [iStart, iEnd] = PolygonRange(rNavData.polygonOffsets, iPoly, static_cast<int32_t>(rNavData.vertices.size()));
		int32_t iCount = iEnd - iStart;

		if (PointInPolygon(f2Point, &pVertices[iStart], iCount))
		{
			return true;
		}
	}
	return false;
}

namespace
{

float Distance(XMFLOAT2 f2A, XMFLOAT2 f2B)
{
	float fDx = f2B.x - f2A.x;
	float fDy = f2B.y - f2A.y;
	return std::sqrt(fDx * fDx + fDy * fDy);
}

// Find the nearest point on any polygon edge to the given position
XMFLOAT2 NearestPolygonEdgePoint(XMFLOAT2 f2Position, const XMFLOAT2* pVertices, const NavData& rNavData)
{
	float fBestDistSq = std::numeric_limits<float>::max();
	XMFLOAT2 f2BestPoint = f2Position;

	for (size_t iPoly = 0; iPoly < rNavData.polygonOffsets.size(); ++iPoly)
	{
		// Broad phase: skip a polygon whose AABB is already farther than the best edge found so far.
		// Index iteration order is preserved so the strict-less-than tie-break below is unchanged.
		const XMFLOAT2& rMin = rNavData.polygonMin.at(iPoly);
		const XMFLOAT2& rMax = rNavData.polygonMax.at(iPoly);
		float fAabbDx = (f2Position.x < rMin.x) ? (rMin.x - f2Position.x) : ((f2Position.x > rMax.x) ? (f2Position.x - rMax.x) : 0.0f);
		float fAabbDy = (f2Position.y < rMin.y) ? (rMin.y - f2Position.y) : ((f2Position.y > rMax.y) ? (f2Position.y - rMax.y) : 0.0f);
		if (fAabbDx * fAabbDx + fAabbDy * fAabbDy > fBestDistSq)
		{
			continue;
		}

		auto [iStart, iEnd] = PolygonRange(rNavData.polygonOffsets, iPoly, static_cast<int32_t>(rNavData.vertices.size()));
		int32_t iCount = iEnd - iStart;

		for (int32_t i = 0; i < iCount; ++i)
		{
			int32_t iNext = (i + 1) % iCount;
			XMFLOAT2 f2A = pVertices[iStart + i];
			XMFLOAT2 f2B = pVertices[iStart + iNext];

			float fEdgeDx = f2B.x - f2A.x;
			float fEdgeDy = f2B.y - f2A.y;
			float fEdgeLenSq = fEdgeDx * fEdgeDx + fEdgeDy * fEdgeDy;

			float fT = 0.0f;
			if (fEdgeLenSq > 1e-10f)
			{
				fT = std::clamp(((f2Position.x - f2A.x) * fEdgeDx + (f2Position.y - f2A.y) * fEdgeDy) / fEdgeLenSq, 0.0f, 1.0f);
			}

			XMFLOAT2 f2Closest {f2A.x + fT * fEdgeDx, f2A.y + fT * fEdgeDy};
			float fDx = f2Closest.x - f2Position.x;
			float fDy = f2Closest.y - f2Position.y;
			float fDistSq = fDx * fDx + fDy * fDy;

			if (fDistSq < fBestDistSq)
			{
				fBestDistSq = fDistSq;
				f2BestPoint = f2Closest;
			}
		}
	}

	return f2BestPoint;
}

// Snap a point inside a polygon to just outside the nearest edge
XMFLOAT2 SnapOutsidePolygon(XMFLOAT2 f2Position, const XMFLOAT2* pVertices, const NavData& rNavData)
{
	static constexpr float kfSnapOffset = 0.5f;
	XMFLOAT2 f2EdgePoint = NearestPolygonEdgePoint(f2Position, pVertices, rNavData);
	float fDx = f2EdgePoint.x - f2Position.x;
	float fDy = f2EdgePoint.y - f2Position.y;
	float fLen = std::sqrt(fDx * fDx + fDy * fDy);
	if (fLen > 1e-6f)
	{
		f2EdgePoint.x += (fDx / fLen) * kfSnapOffset;
		f2EdgePoint.y += (fDy / fLen) * kfSnapOffset;
	}
	return f2EdgePoint;
}

// A* scratch memory layout, allocated as a single contiguous block
struct AStarMemory
{
	float* pGCost = nullptr;
	float* pFCost = nullptr;
	int32_t* pParent = nullptr;
	bool* pClosed = nullptr;
	bool* pStartVisible = nullptr;
	int32_t* pOpenSet = nullptr;  // binary min-heap of node indices
	int32_t* pHeapPos = nullptr;  // per-node position in pOpenSet (-1 = not in heap)
};

struct AStarMemoryLayout
{
	int64_t iGCostOffset = 0;
	int64_t iFCostOffset = 0;
	int64_t iParentOffset = 0;
	int64_t iOpenSetOffset = 0;
	int64_t iHeapPositionOffset = 0;
	int64_t iClosedOffset = 0;
	int64_t iStartVisibleOffset = 0;
	int64_t iByteCount = 0;
};

constexpr AStarMemoryLayout ComputeAStarMemoryLayout(int32_t iTotalNodes, int32_t iVertexCount)
{
	// Layout: all 4-byte types first (float, int32_t), then bool arrays last to avoid alignment issues
	AStarMemoryLayout layout {};
	layout.iFCostOffset = layout.iGCostOffset + static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(float));
	layout.iParentOffset = layout.iFCostOffset + static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(float));
	layout.iOpenSetOffset = layout.iParentOffset + static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(int32_t));
	layout.iHeapPositionOffset = layout.iOpenSetOffset + static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(int32_t));
	layout.iClosedOffset = layout.iHeapPositionOffset + static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(int32_t));
	layout.iStartVisibleOffset = layout.iClosedOffset + static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(bool));
	layout.iByteCount = layout.iStartVisibleOffset + static_cast<int64_t>(iVertexCount) * static_cast<int64_t>(sizeof(bool));
	return layout;
}

AStarMemory BindAStarMemory(std::byte* pMemory, const AStarMemoryLayout& rLayout)
{
	AStarMemory memory
	{
		.pGCost = reinterpret_cast<float*>(pMemory + rLayout.iGCostOffset),
		.pFCost = reinterpret_cast<float*>(pMemory + rLayout.iFCostOffset),
		.pParent = reinterpret_cast<int32_t*>(pMemory + rLayout.iParentOffset),
		.pClosed = reinterpret_cast<bool*>(pMemory + rLayout.iClosedOffset),
		.pStartVisible = reinterpret_cast<bool*>(pMemory + rLayout.iStartVisibleOffset),
		.pOpenSet = reinterpret_cast<int32_t*>(pMemory + rLayout.iOpenSetOffset),
		.pHeapPos = reinterpret_cast<int32_t*>(pMemory + rLayout.iHeapPositionOffset),
	};
	return memory;
}

// Indexed binary min-heap over A* open-set node indices, keyed by (fCost, node index). The index
// tie-break makes pop order a total order, so the search is deterministic across builds. pHeapPos
// enables O(log n) decrease-key. Operates in place on the AStarMemory scratch arrays.
struct AStarHeap
{
	int32_t* pOpenSet = nullptr;
	int32_t* pHeapPos = nullptr;
	const float* pFCost = nullptr;
	int32_t iHeapCount = 0;

	bool Less(int32_t iNodeA, int32_t iNodeB) const
	{
		float fA = pFCost[iNodeA];
		float fB = pFCost[iNodeB];
		if (fA != fB)
		{
			return fA < fB;
		}
		return iNodeA < iNodeB;
	}

	void Swap(int32_t iIndexA, int32_t iIndexB)
	{
		int32_t iNodeA = pOpenSet[iIndexA];
		int32_t iNodeB = pOpenSet[iIndexB];
		pOpenSet[iIndexA] = iNodeB;
		pOpenSet[iIndexB] = iNodeA;
		pHeapPos[iNodeA] = iIndexB;
		pHeapPos[iNodeB] = iIndexA;
	}

	void SiftUp(int32_t iIndex)
	{
		while (iIndex > 0)
		{
			int32_t iParent = (iIndex - 1) / 2;
			if (!Less(pOpenSet[iIndex], pOpenSet[iParent]))
			{
				break;
			}
			Swap(iIndex, iParent);
			iIndex = iParent;
		}
	}

	void SiftDown(int32_t iIndex)
	{
		while (true)
		{
			int32_t iSmallest = iIndex;
			int32_t iLeft = 2 * iIndex + 1;
			int32_t iRight = 2 * iIndex + 2;
			if (iLeft < iHeapCount && Less(pOpenSet[iLeft], pOpenSet[iSmallest]))
			{
				iSmallest = iLeft;
			}
			if (iRight < iHeapCount && Less(pOpenSet[iRight], pOpenSet[iSmallest]))
			{
				iSmallest = iRight;
			}
			if (iSmallest == iIndex)
			{
				break;
			}
			Swap(iIndex, iSmallest);
			iIndex = iSmallest;
		}
	}

	void Push(int32_t iNode)
	{
		pOpenSet[iHeapCount] = iNode;
		pHeapPos[iNode] = iHeapCount;
		++iHeapCount;
		SiftUp(iHeapCount - 1);
	}

	int32_t Pop()
	{
		int32_t iTop = pOpenSet[0];
		pHeapPos[iTop] = -1;
		--iHeapCount;
		if (iHeapCount > 0)
		{
			pOpenSet[0] = pOpenSet[iHeapCount];
			pHeapPos[pOpenSet[0]] = 0;
			SiftDown(0);
		}
		return iTop;
	}
};

// A* pathfinding on the visibility graph with temporary start/end nodes
// Returns the direction toward the first waypoint, or zero vector if no path found
XMVECTOR AStarPath(XMFLOAT2 f2Start, XMFLOAT2 f2End, const XMFLOAT2* pVertices, const NavData& rNavData, const AStarMemory& rMemory, float fBaseHeight, XMVECTOR* pOutNextWaypoint)
{
	int32_t iVertexCount = static_cast<int32_t>(rNavData.vertices.size());
	int32_t iStartNode = iVertexCount;
	int32_t iEndNode = iVertexCount + 1;
	int32_t iTotalNodes = iVertexCount + 2;

	for (int32_t i = 0; i < iTotalNodes; ++i)
	{
		rMemory.pGCost[i] = std::numeric_limits<float>::max();
		rMemory.pFCost[i] = std::numeric_limits<float>::max();
		rMemory.pParent[i] = -1;
		rMemory.pClosed[i] = false;
		rMemory.pHeapPos[i] = -1;
	}

	// Eagerly compute start visibility to all obstacle vertices (fully consumed when the start node pops).
	// End visibility is computed lazily per expanded vertex at the consumption site below: A* usually
	// terminates after a small frontier, and the closed-set guarantees each vertex expands at most once.
	for (int32_t i = 0; i < iVertexCount; ++i)
	{
		rMemory.pStartVisible[i] = !SegmentBlockedByObstacle(f2Start, pVertices[i], pVertices, rNavData);
	}

	// Vertex position lookup (including temporary nodes)
	auto GetPosition = [&](int32_t iNode) -> XMFLOAT2
	{
		if (iNode == iStartNode)
		{
			return f2Start;
		}
		if (iNode == iEndNode)
		{
			return f2End;
		}
		return pVertices[iNode];
	};

	// Binary min-heap open set (see AStarHeap above): keyed by (fCost, node index) with an index
	// tie-break for deterministic pop order; pHeapPos enables O(log n) decrease-key.
	AStarHeap heap {.pOpenSet = rMemory.pOpenSet, .pHeapPos = rMemory.pHeapPos, .pFCost = rMemory.pFCost, .iHeapCount = 0};

	// Initialize start node
	rMemory.pGCost[iStartNode] = 0.0f;
	rMemory.pFCost[iStartNode] = Distance(f2Start, f2End);
	heap.Push(iStartNode);

	while (heap.iHeapCount > 0)
	{
		int32_t iCurrent = heap.Pop();

		if (iCurrent == iEndNode)
		{
			// Reconstruct path: find first waypoint
			int32_t iNode = iEndNode;
			while (rMemory.pParent[iNode] != iStartNode && rMemory.pParent[iNode] != -1)
			{
				iNode = rMemory.pParent[iNode];
			}

			XMFLOAT2 f2Waypoint = GetPosition(iNode);
			if (pOutNextWaypoint != nullptr)
			{
				*pOutNextWaypoint = XMVectorSet(f2Waypoint.x, f2Waypoint.y, fBaseHeight, 1.0f);
			}
			XMVECTOR vecDirection = XMVectorSet(f2Waypoint.x - f2Start.x, f2Waypoint.y - f2Start.y, 0.0f, 0.0f);
			return XMVector3Normalize(vecDirection);
		}

		rMemory.pClosed[iCurrent] = true;

		// Expand neighbors
		auto TryNeighbor = [&](int32_t iNeighbor)
		{
			if (rMemory.pClosed[iNeighbor])
			{
				return;
			}

			XMFLOAT2 f2Current = GetPosition(iCurrent);
			XMFLOAT2 f2Neighbor = GetPosition(iNeighbor);
			float fTentativeG = rMemory.pGCost[iCurrent] + Distance(f2Current, f2Neighbor);

			if (fTentativeG < rMemory.pGCost[iNeighbor])
			{
				rMemory.pGCost[iNeighbor] = fTentativeG;
				rMemory.pFCost[iNeighbor] = fTentativeG + Distance(f2Neighbor, f2End);
				rMemory.pParent[iNeighbor] = iCurrent;

				if (rMemory.pHeapPos[iNeighbor] >= 0)
				{
					heap.SiftUp(rMemory.pHeapPos[iNeighbor]); // fCost decreased -> may move up
				}
				else
				{
					heap.Push(iNeighbor);
				}
			}
		};

		if (iCurrent == iStartNode)
		{
			// The sole caller enters A* only after this post-snap start-to-end segment tested blocked.
			for (int32_t i = 0; i < iVertexCount; ++i)
			{
				if (rMemory.pStartVisible[i])
				{
					TryNeighbor(i);
				}
			}
		}
		else if (iCurrent < iVertexCount)
		{
			// Visibility-graph + polygon-perimeter neighbors, precomputed into one adjacency span.
			int32_t iBegin = rNavData.adjOffsets.at(static_cast<size_t>(iCurrent));
			int32_t iStop = rNavData.adjOffsets.at(static_cast<size_t>(iCurrent) + 1);
			for (int32_t k = iBegin; k < iStop; ++k)
			{
				TryNeighbor(rNavData.adjNeighbors.at(static_cast<size_t>(k)));
			}

			// Lazy end-visibility: computed only for the vertices A* actually expands (each expands at most once).
			if (!SegmentBlockedByObstacle(f2End, pVertices[iCurrent], pVertices, rNavData))
			{
				TryNeighbor(iEndNode);
			}

			if (rMemory.pStartVisible[iCurrent])
			{
				TryNeighbor(iStartNode);
			}
		}
	}

	return XMVectorZero();
}

// A*-miss fallback: slide along the nearest obstacle boundary rather than steer at an obstacle vertex.
// Steering at a vertex sits the unit on a point attractor — it enters the polygon, the start-inside
// branch pushes it back out, and the pair cycles forever. The tangent below is biased outward, so the
// result always carries a strictly positive component along the outward normal of the *nearest*
// boundary and can never drive the unit into it.
// Returns zero when the position sits on that boundary (no defined outward normal); the callers' own
// straight-line fallback then runs.
XMVECTOR NavMissFallbackDirection(XMFLOAT2 f2Position, XMFLOAT2 f2Destination, const XMFLOAT2* pVertices, const NavData& rNavData)
{
	static constexpr float kfNavFallbackOutwardBias = 0.25f;

	XMFLOAT2 f2Edge = NearestPolygonEdgePoint(f2Position, pVertices, rNavData);
	float fOutwardX = f2Position.x - f2Edge.x;
	float fOutwardY = f2Position.y - f2Edge.y;
	float fLength = std::sqrt(fOutwardX * fOutwardX + fOutwardY * fOutwardY);
	if (fLength < 1e-6f)
	{
		return XMVectorZero();
	}
	fOutwardX /= fLength;
	fOutwardY /= fLength;

	// Boundary tangent, signed to whichever way makes progress toward the destination. The comparison is
	// a total order, so the choice is deterministic.
	float fTangentX = -fOutwardY;
	float fTangentY = fOutwardX;
	float fSign = (fTangentX * (f2Destination.x - f2Position.x) + fTangentY * (f2Destination.y - f2Position.y)) < 0.0f ? -1.0f : 1.0f;

	return XMVector3Normalize(XMVectorSet(fTangentX * fSign + fOutwardX * kfNavFallbackOutwardBias, fTangentY * fSign + fOutwardY * kfNavFallbackOutwardBias, 0.0f, 0.0f));
}

} // anonymous namespace

bool XM_CALLCONV NavQueryPointBlocked(FXMVECTOR vecPosition, const NavData& rNavData)
{
	if (rNavData.vertices.empty())
	{
		return false;
	}

	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);
	XMFLOAT2 f2Position {f4Position.x, f4Position.y};

	return PointInAnyPolygon(f2Position, rNavData.vertices.data(), rNavData);
}

XMVECTOR XM_CALLCONV NavQuerySnapToNavigable(FXMVECTOR vecPosition, const NavData& rNavData)
{
	float fBaseHeight = gBaseHeight.Get();
	ASSERT(XMVectorGetZ(vecPosition) == fBaseHeight);

	const XMFLOAT2* pVertices = rNavData.vertices.data();

	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);
	XMFLOAT2 f2Position {f4Position.x, f4Position.y};

	if (!PointInAnyPolygon(f2Position, pVertices, rNavData))
	{
		return vecPosition;
	}

	XMFLOAT2 f2BestPoint = SnapOutsidePolygon(f2Position, pVertices, rNavData);
	return XMVectorSet(f2BestPoint.x, f2BestPoint.y, fBaseHeight, 1.0f);
}

XMVECTOR XM_CALLCONV NavQueryDirection(FXMVECTOR vecPosition, FXMVECTOR vecDestination, const NavData& rNavData, XMVECTOR* pOutNextWaypoint
#if defined(BT_SERVER)
	, bool* pOutEnteredAStar
#endif // BT_SERVER
)
{
#if defined(BT_SERVER)
	if constexpr (kbProfiling)
	{
		if (pOutEnteredAStar != nullptr)
		{
			*pOutEnteredAStar = false;
		}
	}
#endif // BT_SERVER

	float fBaseHeight = gBaseHeight.Get();
	ASSERT(XMVectorGetZ(vecPosition) == fBaseHeight);
	ASSERT(XMVectorGetZ(vecDestination) == fBaseHeight);

	// Default waypoint = destination (W=1.0 position). Later paths may overwrite with a refined
	// intermediate waypoint, but this guarantees the out-param is always a valid W=1 position
	// even when early-out branches (zero-delta, empty navData, A*-miss) skip the explicit writes.
	if (pOutNextWaypoint != nullptr)
	{
		*pOutNextWaypoint = XMVectorSetW(vecDestination, 1.0f);
	}

	XMVECTOR vecDelta = XMVectorSubtract(vecDestination, vecPosition);
	if (XMVectorGetX(XMVector3LengthSq(vecDelta)) < 1e-8f)
	{
		return XMVectorZero();
	}

	if (rNavData.vertices.empty())
	{
		return XMVector3Normalize(vecDelta);
	}

	int32_t iVertexCount = static_cast<int32_t>(rNavData.vertices.size());
	int32_t iTotalNodes = iVertexCount + 2;

	// Workbuffer allocation for A* scratch memory only (vertices already world-space in NavData)
	AStarMemoryLayout aStarMemoryLayout = ComputeAStarMemoryLayout(iTotalNodes, iVertexCount);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	auto pMemory = rWorkbuffer.PushBuffer<std::byte*>(aStarMemoryLayout.iByteCount);
	AStarMemory aStarMemory = BindAStarMemory(pMemory, aStarMemoryLayout);

	const XMFLOAT2* pVertices = rNavData.vertices.data();

	XMFLOAT4A f4Position {};
	XMStoreFloat4A(&f4Position, vecPosition);
	XMFLOAT4A f4Destination {};
	XMStoreFloat4A(&f4Destination, vecDestination);

	XMFLOAT2 f2Position {f4Position.x, f4Position.y};
	XMFLOAT2 f2Destination {f4Destination.x, f4Destination.y};

	XMVECTOR vecResult = XMVectorZero();

	// If start is inside an obstacle, direct toward nearest polygon edge to escape first
	if (PointInAnyPolygon(f2Position, pVertices, rNavData))
	{
		XMFLOAT2 f2SnapPoint = SnapOutsidePolygon(f2Position, pVertices, rNavData);
		XMVECTOR vecEscape = XMVectorSet(f2SnapPoint.x - f4Position.x, f2SnapPoint.y - f4Position.y, 0.0f, 0.0f);
		if (XMVectorGetX(XMVector3LengthSq(vecEscape)) > 1e-8f)
		{
			if (pOutNextWaypoint != nullptr)
			{
				*pOutNextWaypoint = XMVectorSet(f2SnapPoint.x, f2SnapPoint.y, fBaseHeight, 1.0f);
			}
			return XMVector3Normalize(vecEscape);
		}
		f2Position = f2SnapPoint;
	}

	// If destination is inside an obstacle, snap it to nearest navigable point
	if (PointInAnyPolygon(f2Destination, pVertices, rNavData))
	{
		f2Destination = SnapOutsidePolygon(f2Destination, pVertices, rNavData);
	}

	// Fast path: direct line of sight
	if (!SegmentBlockedByObstacle(f2Position, f2Destination, pVertices, rNavData))
	{
		if (pOutNextWaypoint != nullptr)
		{
			*pOutNextWaypoint = XMVectorSet(f2Destination.x, f2Destination.y, fBaseHeight, 1.0f);
		}
		vecResult = XMVector3Normalize(XMVectorSet(f2Destination.x - f2Position.x, f2Destination.y - f2Position.y, 0.0f, 0.0f));
	}
	else
	{
		// A* pathfinding on visibility graph
#if defined(BT_SERVER)
		if constexpr (kbProfiling)
		{
			if (pOutEnteredAStar != nullptr)
			{
				*pOutEnteredAStar = true;
			}
		}
#endif // BT_SERVER
		vecResult = AStarPath(f2Position, f2Destination, pVertices, rNavData, aStarMemory, fBaseHeight, pOutNextWaypoint);

		// Fallback: if A* found no path, slide along the nearest obstacle boundary. The visibility graph
		// spans the whole cell, so a miss means it is disconnected — an invariant violation worth a warning.
		if (XMVectorGetX(XMVector3LengthSq(vecResult)) < 1e-8f)
		{
			LOG(kNavData, kWarning, "NavQuery A* found no path: position={} destination={} vertices={}", common::WbV2(XMLoadFloat2(&f2Position), 2), common::WbV2(XMLoadFloat2(&f2Destination), 2), iVertexCount);
			vecResult = NavMissFallbackDirection(f2Position, f2Destination, pVertices, rNavData);
		}
	}

	return vecResult;
}

} // namespace engine
