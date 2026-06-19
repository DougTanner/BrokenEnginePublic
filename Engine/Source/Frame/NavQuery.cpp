#include "NavQuery.h"

#include "NavBuild.h"
#include "Ui/WrapperBase.h"

namespace engine
{

namespace
{

// Segment-segment intersection test (proper intersection, excluding endpoints)
bool SegmentsIntersect(XMFLOAT2 f2A1, XMFLOAT2 f2A2, XMFLOAT2 f2B1, XMFLOAT2 f2B2)
{
	float fD1x = f2A2.x - f2A1.x;
	float fD1y = f2A2.y - f2A1.y;
	float fD2x = f2B2.x - f2B1.x;
	float fD2y = f2B2.y - f2B1.y;

	float fDenom = fD1x * fD2y - fD1y * fD2x;
	if (std::abs(fDenom) < 1e-10f)
	{
		return false;
	}

	float fDiffX = f2B1.x - f2A1.x;
	float fDiffY = f2B1.y - f2A1.y;

	float fT = (fDiffX * fD2y - fDiffY * fD2x) / fDenom;
	float fU = (fDiffX * fD1y - fDiffY * fD1x) / fDenom;

	static constexpr float kfSegmentEpsilon = 1e-6f;
	return fT > kfSegmentEpsilon && fT < (1.0f - kfSegmentEpsilon) && fU > kfSegmentEpsilon && fU < (1.0f - kfSegmentEpsilon);
}

// Test if a segment intersects any obstacle edge, using the NavData edge grid as a broad phase. The
// result is an order-independent boolean OR, so grid-traversal order never changes it -> deterministic.
// Precondition: rNavData has been through BuildNavAcceleration with a non-empty vertex set (the grid CSR
// is empty otherwise). All callers reach here only after NavQueryDirection's empty-vertices early-out.
// Soundness: edges are bucketed into every cell their AABB overlaps (conservative), and the DDA walks
// every cell the clipped segment passes through, so a real crossing is always found.
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

// Point-in-polygon test using winding number
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

		int32_t iStart = rNavData.polygonOffsets.at(iPoly);
		int32_t iEnd = (iPoly + 1 < rNavData.polygonOffsets.size()) ? rNavData.polygonOffsets.at(iPoly + 1) : static_cast<int32_t>(rNavData.vertices.size());
		int32_t iCount = iEnd - iStart;

		int32_t iWinding = 0;
		for (int32_t i = 0; i < iCount; ++i)
		{
			int32_t iNext = (i + 1) % iCount;
			XMFLOAT2 f2A = pVertices[iStart + i];
			XMFLOAT2 f2B = pVertices[iStart + iNext];

			if (f2A.y <= f2Point.y)
			{
				if (f2B.y > f2Point.y)
				{
					float fCross = (f2B.x - f2A.x) * (f2Point.y - f2A.y) - (f2Point.x - f2A.x) * (f2B.y - f2A.y);
					if (fCross > 0.0f)
					{
						++iWinding;
					}
				}
			}
			else
			{
				if (f2B.y <= f2Point.y)
				{
					float fCross = (f2B.x - f2A.x) * (f2Point.y - f2A.y) - (f2Point.x - f2A.x) * (f2B.y - f2A.y);
					if (fCross < 0.0f)
					{
						--iWinding;
					}
				}
			}
		}
		if (iWinding != 0)
		{
			return true;
		}
	}
	return false;
}

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

		int32_t iStart = rNavData.polygonOffsets.at(iPoly);
		int32_t iEnd = (iPoly + 1 < rNavData.polygonOffsets.size()) ? rNavData.polygonOffsets.at(iPoly + 1) : static_cast<int32_t>(rNavData.vertices.size());
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
	bool* pEndVisible = nullptr;
	int32_t* pOpenSet = nullptr;  // binary min-heap of node indices
	int32_t* pHeapPos = nullptr;  // per-node position in pOpenSet (-1 = not in heap)
};

constexpr int64_t ComputeAStarMemorySize(int32_t iTotalNodes, int32_t iVertexCount)
{
	// Layout: all 4-byte types first (float, int32_t), then bool arrays last to avoid alignment issues
	int64_t iSize = 0;
	iSize += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(float));     // pGCost
	iSize += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(float));     // pFCost
	iSize += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(int32_t));   // pParent
	iSize += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(int32_t));   // pOpenSet
	iSize += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(int32_t));   // pHeapPos
	iSize += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(bool));      // pClosed
	iSize += static_cast<int64_t>(iVertexCount) * static_cast<int64_t>(sizeof(bool));     // pStartVisible
	iSize += static_cast<int64_t>(iVertexCount) * static_cast<int64_t>(sizeof(bool));     // pEndVisible
	return iSize;
}

AStarMemory PartitionAStarMemory(std::byte* pMemory, int32_t iTotalNodes, int32_t iVertexCount)
{
	// Layout: all 4-byte types first, then bool arrays last to avoid alignment issues
	AStarMemory memory {};
	memory.pGCost = reinterpret_cast<float*>(pMemory);
	pMemory += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(float));
	memory.pFCost = reinterpret_cast<float*>(pMemory);
	pMemory += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(float));
	memory.pParent = reinterpret_cast<int32_t*>(pMemory);
	pMemory += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(int32_t));
	memory.pOpenSet = reinterpret_cast<int32_t*>(pMemory);
	pMemory += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(int32_t));
	memory.pHeapPos = reinterpret_cast<int32_t*>(pMemory);
	pMemory += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(int32_t));
	memory.pClosed = reinterpret_cast<bool*>(pMemory);
	pMemory += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(bool));
	memory.pStartVisible = reinterpret_cast<bool*>(pMemory);
	pMemory += static_cast<int64_t>(iVertexCount) * static_cast<int64_t>(sizeof(bool));
	memory.pEndVisible = reinterpret_cast<bool*>(pMemory);
	return memory;
}

// A* pathfinding on the visibility graph with temporary start/end nodes
// Returns the direction toward the first waypoint, or zero vector if no path found
XMVECTOR AStarPath(XMFLOAT2 f2Start, XMFLOAT2 f2End, const XMFLOAT2* pVertices, const NavData& rNavData, const AStarMemory& rMemory, XMVECTOR* pOutNextWaypoint)
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

	// Find visibility from start and end to all obstacle vertices
	for (int32_t i = 0; i < iVertexCount; ++i)
	{
		rMemory.pStartVisible[i] = !SegmentBlockedByObstacle(f2Start, pVertices[i], pVertices, rNavData);
		rMemory.pEndVisible[i] = !SegmentBlockedByObstacle(f2End, pVertices[i], pVertices, rNavData);
	}

	// Vertex position lookup (including temporary nodes)
	auto GetPosition = [&](int32_t iNode) -> XMFLOAT2
	{
		if (iNode == iStartNode) return f2Start;
		if (iNode == iEndNode) return f2End;
		return pVertices[iNode];
	};

	// Binary min-heap open set keyed by (fCost, node index). The index tie-break makes pop order a
	// total order, so the search is deterministic across builds. pHeapPos enables O(log n) decrease-key.
	int32_t iHeapCount = 0;

	auto HeapLess = [&](int32_t iNodeA, int32_t iNodeB) -> bool
	{
		float fA = rMemory.pFCost[iNodeA];
		float fB = rMemory.pFCost[iNodeB];
		if (fA != fB)
		{
			return fA < fB;
		}
		return iNodeA < iNodeB;
	};

	auto HeapSwap = [&](int32_t iIndexA, int32_t iIndexB)
	{
		int32_t iNodeA = rMemory.pOpenSet[iIndexA];
		int32_t iNodeB = rMemory.pOpenSet[iIndexB];
		rMemory.pOpenSet[iIndexA] = iNodeB;
		rMemory.pOpenSet[iIndexB] = iNodeA;
		rMemory.pHeapPos[iNodeA] = iIndexB;
		rMemory.pHeapPos[iNodeB] = iIndexA;
	};

	auto SiftUp = [&](int32_t iIndex)
	{
		while (iIndex > 0)
		{
			int32_t iParent = (iIndex - 1) / 2;
			if (!HeapLess(rMemory.pOpenSet[iIndex], rMemory.pOpenSet[iParent]))
			{
				break;
			}
			HeapSwap(iIndex, iParent);
			iIndex = iParent;
		}
	};

	auto SiftDown = [&](int32_t iIndex)
	{
		while (true)
		{
			int32_t iSmallest = iIndex;
			int32_t iLeft = 2 * iIndex + 1;
			int32_t iRight = 2 * iIndex + 2;
			if (iLeft < iHeapCount && HeapLess(rMemory.pOpenSet[iLeft], rMemory.pOpenSet[iSmallest]))
			{
				iSmallest = iLeft;
			}
			if (iRight < iHeapCount && HeapLess(rMemory.pOpenSet[iRight], rMemory.pOpenSet[iSmallest]))
			{
				iSmallest = iRight;
			}
			if (iSmallest == iIndex)
			{
				break;
			}
			HeapSwap(iIndex, iSmallest);
			iIndex = iSmallest;
		}
	};

	auto HeapPush = [&](int32_t iNode)
	{
		rMemory.pOpenSet[iHeapCount] = iNode;
		rMemory.pHeapPos[iNode] = iHeapCount;
		++iHeapCount;
		SiftUp(iHeapCount - 1);
	};

	auto HeapPop = [&]() -> int32_t
	{
		int32_t iTop = rMemory.pOpenSet[0];
		rMemory.pHeapPos[iTop] = -1;
		--iHeapCount;
		if (iHeapCount > 0)
		{
			rMemory.pOpenSet[0] = rMemory.pOpenSet[iHeapCount];
			rMemory.pHeapPos[rMemory.pOpenSet[0]] = 0;
			SiftDown(0);
		}
		return iTop;
	};

	// Initialize start node
	rMemory.pGCost[iStartNode] = 0.0f;
	rMemory.pFCost[iStartNode] = Distance(f2Start, f2End);
	HeapPush(iStartNode);

	while (iHeapCount > 0)
	{
		int32_t iCurrent = HeapPop();

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
				*pOutNextWaypoint = XMVectorSet(f2Waypoint.x, f2Waypoint.y, gBaseHeight.Get(), 1.0f);
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
					SiftUp(rMemory.pHeapPos[iNeighbor]); // fCost decreased -> may move up
				}
				else
				{
					HeapPush(iNeighbor);
				}
			}
		};

		if (iCurrent == iStartNode)
		{
			for (int32_t i = 0; i < iVertexCount; ++i)
			{
				if (rMemory.pStartVisible[i])
				{
					TryNeighbor(i);
				}
			}
			if (!SegmentBlockedByObstacle(f2Start, f2End, pVertices, rNavData))
			{
				TryNeighbor(iEndNode);
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

			if (rMemory.pEndVisible[iCurrent])
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

} // anonymous namespace

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

XMVECTOR XM_CALLCONV NavQueryDirection(FXMVECTOR vecPosition, FXMVECTOR vecDestination, const NavData& rNavData, XMVECTOR* pOutNextWaypoint)
{
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
	int64_t iAStarBytes = ComputeAStarMemorySize(iTotalNodes, iVertexCount);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	auto pMemory = rWorkbuffer.PushBuffer<std::byte*>(iAStarBytes);
	AStarMemory aStarMemory = PartitionAStarMemory(pMemory, iTotalNodes, iVertexCount);

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
		vecResult = AStarPath(f2Position, f2Destination, pVertices, rNavData, aStarMemory, pOutNextWaypoint);

		// Fallback: if A* found no path, move toward nearest visible obstacle vertex
		if (XMVectorGetX(XMVector3LengthSq(vecResult)) < 1e-8f)
		{
			float fBestDist = std::numeric_limits<float>::max();
			XMFLOAT2 f2BestVertex = f2Position;
			bool bFound = false;

			for (int32_t i = 0; i < iVertexCount; ++i)
			{
				if (!SegmentBlockedByObstacle(f2Position, pVertices[i], pVertices, rNavData))
				{
					float fDist = Distance(f2Position, pVertices[i]);
					if (fDist < fBestDist)
					{
						fBestDist = fDist;
						f2BestVertex = pVertices[i];
						bFound = true;
					}
				}
			}

			if (bFound)
			{
				if (pOutNextWaypoint != nullptr)
				{
					*pOutNextWaypoint = XMVectorSet(f2BestVertex.x, f2BestVertex.y, fBaseHeight, 1.0f);
				}
				vecResult = XMVector3Normalize(XMVectorSet(f2BestVertex.x - f2Position.x, f2BestVertex.y - f2Position.y, 0.0f, 0.0f));
			}
		}
	}

	return vecResult;
}

} // namespace engine
