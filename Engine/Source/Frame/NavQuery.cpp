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

// Test if a segment intersects any polygon edge
bool SegmentBlockedByObstacle(XMFLOAT2 f2A, XMFLOAT2 f2B, const XMFLOAT2* pVertices, const NavData& rNavData)
{
	for (size_t iPoly = 0; iPoly < rNavData.polygonOffsets.size(); ++iPoly)
	{
		int32_t iStart = rNavData.polygonOffsets.at(iPoly);
		int32_t iEnd = (iPoly + 1 < rNavData.polygonOffsets.size()) ? rNavData.polygonOffsets.at(iPoly + 1) : static_cast<int32_t>(rNavData.vertices.size());
		int32_t iCount = iEnd - iStart;

		for (int32_t i = 0; i < iCount; ++i)
		{
			int32_t iNext = (i + 1) % iCount;
			if (SegmentsIntersect(f2A, f2B, pVertices[iStart + i], pVertices[iStart + iNext]))
			{
				return true;
			}
		}
	}
	return false;
}

// Point-in-polygon test using winding number
bool PointInAnyPolygon(XMFLOAT2 f2Point, const XMFLOAT2* pVertices, const NavData& rNavData)
{
	for (size_t iPoly = 0; iPoly < rNavData.polygonOffsets.size(); ++iPoly)
	{
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
	int32_t* pOpenSet = nullptr;
};

int64_t ComputeAStarMemorySize(int32_t iTotalNodes, int32_t iVertexCount)
{
	// Layout: all 4-byte types first (float, int32_t), then bool arrays last to avoid alignment issues
	int64_t iSize = 0;
	iSize += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(float));     // pGCost
	iSize += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(float));     // pFCost
	iSize += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(int32_t));   // pParent
	iSize += static_cast<int64_t>(iTotalNodes) * static_cast<int64_t>(sizeof(int32_t));   // pOpenSet
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

	// Initialize start node
	rMemory.pGCost[iStartNode] = 0.0f;
	rMemory.pFCost[iStartNode] = Distance(f2Start, f2End);

	int32_t iOpenCount = 1;
	rMemory.pOpenSet[0] = iStartNode;

	while (iOpenCount > 0)
	{
		// Find node with lowest f-cost in open set
		int32_t iBestIndex = 0;
		float fBestF = rMemory.pFCost[rMemory.pOpenSet[0]];
		for (int32_t i = 1; i < iOpenCount; ++i)
		{
			if (rMemory.pFCost[rMemory.pOpenSet[i]] < fBestF)
			{
				fBestF = rMemory.pFCost[rMemory.pOpenSet[i]];
				iBestIndex = i;
			}
		}

		int32_t iCurrent = rMemory.pOpenSet[iBestIndex];

		// Remove from open set
		rMemory.pOpenSet[iBestIndex] = rMemory.pOpenSet[iOpenCount - 1];
		--iOpenCount;

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
				*pOutNextWaypoint = XMVectorSet(f2Waypoint.x, f2Waypoint.y, gBaseHeight.Get(), 0.0f);
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

				// Add to open set if not already present
				bool bInOpen = false;
				for (int32_t i = 0; i < iOpenCount; ++i)
				{
					if (rMemory.pOpenSet[i] == iNeighbor)
					{
						bInOpen = true;
						break;
					}
				}
				if (!bInOpen)
				{
					rMemory.pOpenSet[iOpenCount] = iNeighbor;
					++iOpenCount;
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
			// Visibility graph edges
			for (size_t i = 0; i < rNavData.visEdgeA.size(); ++i)
			{
				if (rNavData.visEdgeA.at(i) == iCurrent)
				{
					TryNeighbor(rNavData.visEdgeB.at(i));
				}
				else if (rNavData.visEdgeB.at(i) == iCurrent)
				{
					TryNeighbor(rNavData.visEdgeA.at(i));
				}
			}

			// Adjacent polygon vertices (polygon edges are walkable)
			for (size_t iPoly = 0; iPoly < rNavData.polygonOffsets.size(); ++iPoly)
			{
				int32_t iPolyStart = rNavData.polygonOffsets.at(iPoly);
				int32_t iPolyEnd = (iPoly + 1 < rNavData.polygonOffsets.size()) ? rNavData.polygonOffsets.at(iPoly + 1) : iVertexCount;
				int32_t iCount = iPolyEnd - iPolyStart;

				if (iCurrent >= iPolyStart && iCurrent < iPolyEnd)
				{
					int32_t iLocal = iCurrent - iPolyStart;
					TryNeighbor(iPolyStart + (iLocal + 1) % iCount);
					TryNeighbor(iPolyStart + (iLocal + iCount - 1) % iCount);
					break;
				}
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
	return XMVectorSet(f2BestPoint.x, f2BestPoint.y, fBaseHeight, XMVectorGetW(vecPosition));
}

XMVECTOR XM_CALLCONV NavQueryDirection(FXMVECTOR vecPosition, FXMVECTOR vecDestination, const NavData& rNavData, XMVECTOR* pOutNextWaypoint)
{
	float fBaseHeight = gBaseHeight.Get();
	ASSERT(XMVectorGetZ(vecPosition) == fBaseHeight);
	ASSERT(XMVectorGetZ(vecDestination) == fBaseHeight);

	XMVECTOR vecDelta = XMVectorSubtract(vecDestination, vecPosition);
	if (XMVector3LengthSq(vecDelta).m128_f32[0] < 1e-8f)
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
	std::byte* pMemory = rWorkbuffer.PushBuffer<std::byte*>(iAStarBytes);
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
		if (XMVector3LengthSq(vecEscape).m128_f32[0] > 1e-8f)
		{
			if (pOutNextWaypoint != nullptr)
			{
				*pOutNextWaypoint = XMVectorSet(f2SnapPoint.x, f2SnapPoint.y, fBaseHeight, 0.0f);
			}
			rWorkbuffer.Pop();
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
			*pOutNextWaypoint = XMVectorSet(f2Destination.x, f2Destination.y, fBaseHeight, 0.0f);
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
					*pOutNextWaypoint = XMVectorSet(f2BestVertex.x, f2BestVertex.y, fBaseHeight, 0.0f);
				}
				vecResult = XMVector3Normalize(XMVectorSet(f2BestVertex.x - f2Position.x, f2BestVertex.y - f2Position.y, 0.0f, 0.0f));
			}
		}
	}

	rWorkbuffer.Pop();
	return vecResult;
}

} // namespace engine
