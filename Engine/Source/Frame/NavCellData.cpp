#include "NavBuild.h"

#include "Frame/IslandChainPlacement.h"
#include "Frame/IslandTerrain.h"
#include "NavBuildInternal.h"

namespace engine
{

namespace
{

// Detect crossing polygon edges (the yellow debug lines). Two non-adjacent polygon edges that properly
// intersect indicate a self-intersecting contour. Cross-placement overlap is no longer a defect on its
// own: the visibility build and the runtime query both evaluate every polygon in the cell in world
// space, so an edge that clears one island is still tested against the rest. Pure diagnostic:
// O(edges^2) double loop, compiled out by default. Flip kbDebugNavCrossingCheck to true locally when
// investigating a suspected contour defect.
void DebugCheckCrossingEdges([[maybe_unused]] const NavData& rNavData)
{
	if constexpr (kbDebugNavCrossingCheck)
	{
		int32_t iVertexCount = static_cast<int32_t>(rNavData.vertices.size());
		int32_t iPolygonCount = static_cast<int32_t>(rNavData.polygonOffsets.size());
		for (int32_t iPolyA = 0; iPolyA < iPolygonCount; ++iPolyA)
		{
			auto [iStartA, iEndA] = PolygonRange(rNavData.polygonOffsets, iPolyA, iVertexCount);
			int32_t iCountA = iEndA - iStartA;
			if (iCountA < 2)
			{
				continue;
			}

			for (int32_t iEdgeA = 0; iEdgeA < iCountA; ++iEdgeA)
			{
				int32_t iA0 = iStartA + iEdgeA;
				int32_t iA1 = iStartA + ((iEdgeA + 1) % iCountA);
				XMFLOAT2 f2A0 = rNavData.vertices.at(iA0);
				XMFLOAT2 f2A1 = rNavData.vertices.at(iA1);

				for (int32_t iPolyB = iPolyA; iPolyB < iPolygonCount; ++iPolyB)
				{
					auto [iStartB, iEndB] = PolygonRange(rNavData.polygonOffsets, iPolyB, iVertexCount);
					int32_t iCountB = iEndB - iStartB;
					if (iCountB < 2)
					{
						continue;
					}

					int32_t iFirstEdgeB = (iPolyB == iPolyA) ? (iEdgeA + 1) : 0;
					for (int32_t iEdgeB = iFirstEdgeB; iEdgeB < iCountB; ++iEdgeB)
					{
						int32_t iB0 = iStartB + iEdgeB;
						int32_t iB1 = iStartB + ((iEdgeB + 1) % iCountB);
						XMFLOAT2 f2B0 = rNavData.vertices.at(iB0);
						XMFLOAT2 f2B1 = rNavData.vertices.at(iB1);

						if (SegmentsIntersect(f2A0, f2A1, f2B0, f2B1))
						{
							LOG(kNavData, kError, "[DEBUG-nav-crossing] NavData crossing polygon edges: polyA={} edgeA=({}->{}) ({} {})-({} {}) | polyB={} edgeB=({}->{}) ({} {})-({} {})", iPolyA, iA0, iA1, common::Wb(f2A0.x, 4), common::Wb(f2A0.y, 4), common::Wb(f2A1.x, 4), common::Wb(f2A1.y, 4), iPolyB, iB0, iB1, common::Wb(f2B0.x, 4), common::Wb(f2B0.y, 4), common::Wb(f2B1.x, 4), common::Wb(f2B1.y, 4));
							DEBUG_BREAK();
						}
					}
				}
			}
		}
	}
}

// Edge grid (CSR): bucket each obstacle edge into every cell its AABB overlaps (conservative). Reads
// the global vertex AABB off rNavData.gridMin/gridMax (set by the caller). The gridEdges fill is
// cursor-driven, so its insertion order must be preserved verbatim — the rebuild is deterministic on
// both client and server, and the broad phase feeds A* steering CRC'd positions.
void BuildNavEdgeGrid(NavData& rNavData)
{
	float fMinX = rNavData.gridMin.x;
	float fMinY = rNavData.gridMin.y;
	float fMaxX = rNavData.gridMax.x;
	float fMaxY = rNavData.gridMax.y;

	int32_t iEdgeCount = static_cast<int32_t>(rNavData.edgeA.size());
	int32_t iCellCount = kiNavZonesX * kiNavZonesY;

	auto EdgeCellRange = [&](int32_t iEdge, int32_t& riCx0, int32_t& riCx1, int32_t& riCy0, int32_t& riCy1)
	{
		const XMFLOAT2& rA = rNavData.vertices.at(rNavData.edgeA.at(iEdge));
		const XMFLOAT2& rB = rNavData.vertices.at(rNavData.edgeB.at(iEdge));
		riCx0 = NavGridCell(std::min(rA.x, rB.x), fMinX, fMaxX, kiNavZonesX);
		riCx1 = NavGridCell(std::max(rA.x, rB.x), fMinX, fMaxX, kiNavZonesX);
		riCy0 = NavGridCell(std::min(rA.y, rB.y), fMinY, fMaxY, kiNavZonesY);
		riCy1 = NavGridCell(std::max(rA.y, rB.y), fMinY, fMaxY, kiNavZonesY);
	};

	std::vector<int32_t> gridCounts(static_cast<size_t>(iCellCount), 0);
	for (int32_t iEdge = 0; iEdge < iEdgeCount; ++iEdge)
	{
		int32_t iCx0 = 0;
		int32_t iCx1 = 0;
		int32_t iCy0 = 0;
		int32_t iCy1 = 0;
		EdgeCellRange(iEdge, iCx0, iCx1, iCy0, iCy1);
		for (int32_t iCy = iCy0; iCy <= iCy1; ++iCy)
		{
			for (int32_t iCx = iCx0; iCx <= iCx1; ++iCx)
			{
				++gridCounts.at(static_cast<size_t>(iCy) * kiNavZonesX + static_cast<size_t>(iCx));
			}
		}
	}

	rNavData.gridEdgeOffsets.resize(static_cast<size_t>(iCellCount) + 1);
	rNavData.gridEdgeOffsets.at(0) = 0;
	for (int32_t iCell = 0; iCell < iCellCount; ++iCell)
	{
		rNavData.gridEdgeOffsets.at(static_cast<size_t>(iCell) + 1) = rNavData.gridEdgeOffsets.at(static_cast<size_t>(iCell)) + gridCounts.at(static_cast<size_t>(iCell));
	}

	rNavData.gridEdges.resize(static_cast<size_t>(rNavData.gridEdgeOffsets.at(static_cast<size_t>(iCellCount))));
	std::vector<int32_t> gridCursor(rNavData.gridEdgeOffsets.begin(), rNavData.gridEdgeOffsets.end() - 1);
	for (int32_t iEdge = 0; iEdge < iEdgeCount; ++iEdge)
	{
		int32_t iCx0 = 0;
		int32_t iCx1 = 0;
		int32_t iCy0 = 0;
		int32_t iCy1 = 0;
		EdgeCellRange(iEdge, iCx0, iCx1, iCy0, iCy1);
		for (int32_t iCy = iCy0; iCy <= iCy1; ++iCy)
		{
			for (int32_t iCx = iCx0; iCx <= iCx1; ++iCx)
			{
				rNavData.gridEdges.at(static_cast<size_t>(gridCursor.at(static_cast<size_t>(iCy) * kiNavZonesX + static_cast<size_t>(iCx))++)) = iEdge;
			}
		}
	}
}

// Per-vertex adjacency CSR (visibility-graph neighbors + polygon prev/next). Each vertex's neighbor
// span is sorted by index so A* neighbor iteration is order-stable -> deterministic across builds.
void BuildNavAdjacency(NavData& rNavData)
{
	int32_t iVertexCount = static_cast<int32_t>(rNavData.vertices.size());
	int32_t iPolygonCount = static_cast<int32_t>(rNavData.polygonOffsets.size());

	int32_t iVisCount = static_cast<int32_t>(rNavData.visEdgeA.size());
	std::vector<int32_t> adjCounts(static_cast<size_t>(iVertexCount), 0);
	for (int32_t iVis = 0; iVis < iVisCount; ++iVis)
	{
		++adjCounts.at(static_cast<size_t>(rNavData.visEdgeA.at(iVis)));
		++adjCounts.at(static_cast<size_t>(rNavData.visEdgeB.at(iVis)));
	}
	for (int32_t iPoly = 0; iPoly < iPolygonCount; ++iPoly)
	{
		auto [iStart, iEnd] = PolygonRange(rNavData.polygonOffsets, iPoly, iVertexCount);
		if (iEnd - iStart < 2)
		{
			continue;
		}
		for (int32_t i = iStart; i < iEnd; ++i)
		{
			adjCounts.at(static_cast<size_t>(i)) += 2; // prev + next
		}
	}

	rNavData.adjOffsets.resize(static_cast<size_t>(iVertexCount) + 1);
	rNavData.adjOffsets.at(0) = 0;
	for (int32_t iVertex = 0; iVertex < iVertexCount; ++iVertex)
	{
		rNavData.adjOffsets.at(static_cast<size_t>(iVertex) + 1) = rNavData.adjOffsets.at(static_cast<size_t>(iVertex)) + adjCounts.at(static_cast<size_t>(iVertex));
	}
	rNavData.adjNeighbors.resize(static_cast<size_t>(rNavData.adjOffsets.at(static_cast<size_t>(iVertexCount))));
	std::vector<int32_t> adjCursor(rNavData.adjOffsets.begin(), rNavData.adjOffsets.end() - 1);

	auto AddNeighbor = [&](int32_t iVertex, int32_t iNeighbor)
	{
		rNavData.adjNeighbors.at(static_cast<size_t>(adjCursor.at(static_cast<size_t>(iVertex))++)) = iNeighbor;
	};
	for (int32_t iVis = 0; iVis < iVisCount; ++iVis)
	{
		int32_t iA = rNavData.visEdgeA.at(iVis);
		int32_t iB = rNavData.visEdgeB.at(iVis);
		AddNeighbor(iA, iB);
		AddNeighbor(iB, iA);
	}
	for (int32_t iPoly = 0; iPoly < iPolygonCount; ++iPoly)
	{
		auto [iStart, iEnd] = PolygonRange(rNavData.polygonOffsets, iPoly, iVertexCount);
		int32_t iCount = iEnd - iStart;
		if (iCount < 2)
		{
			continue;
		}
		for (int32_t i = 0; i < iCount; ++i)
		{
			AddNeighbor(iStart + i, iStart + ((i + 1) % iCount));
			AddNeighbor(iStart + i, iStart + ((i + iCount - 1) % iCount));
		}
	}

	// Sort each vertex's neighbor span by index so A* neighbor iteration is order-stable.
	for (int32_t iVertex = 0; iVertex < iVertexCount; ++iVertex)
	{
		int32_t iBegin = rNavData.adjOffsets.at(static_cast<size_t>(iVertex));
		int32_t iStop = rNavData.adjOffsets.at(static_cast<size_t>(iVertex) + 1);
		std::sort(rNavData.adjNeighbors.begin() + iBegin, rNavData.adjNeighbors.begin() + iStop);
	}
}

// Whole-cell visibility graph in world space: edge (i, j) exists iff the runtime's own blocked test says
// the segment is clear of every polygon in the cell, so the build and the query can never disagree.
// Determinism: emission is fixed (i, j) index order over deterministic input, and SegmentBlockedByObstacle
// is an order-independent boolean OR. Precondition: BuildNavAcceleration has already run over a non-empty
// vertex set — the blocked test reads the gridMin/gridMax and edge CSR it produces.
void BuildCellVisibilityGraph(NavData& rNavData)
{
	int32_t iVertexCount = static_cast<int32_t>(rNavData.vertices.size());
	const XMFLOAT2* pVertices = rNavData.vertices.data();

	// Per-vertex polygon membership and convexity, precomputed once (O(V)) so the O(V^2) pair loop below
	// runs O(1) per pair instead of re-scanning polygonOffsets for every (i, j).
	std::vector<int32_t> vertexPolygon(static_cast<size_t>(iVertexCount), 0);
	std::vector<int32_t> vertexLocal(static_cast<size_t>(iVertexCount), 0);
	std::vector<int32_t> vertexPolygonCount(static_cast<size_t>(iVertexCount), 0);
	std::vector<bool> vertexConvex(static_cast<size_t>(iVertexCount), false);
	for (size_t iPoly = 0; iPoly < rNavData.polygonOffsets.size(); ++iPoly)
	{
		auto [iStart, iEnd] = PolygonRange(rNavData.polygonOffsets, iPoly, iVertexCount);
		int32_t iCount = iEnd - iStart;

		// Convexity sign from the measured world-space winding, never assumed. BuildCellNavData below maps
		// fLocalY = (0.5f - fV) * fFootprintY, mirroring Y, and the rotation preserves orientation — so
		// these merged world-space polygons wind clockwise, the reverse of the UV-space template contour
		// NavBuild.cpp asserts CCW. A CCW assumption here would select exactly the reflex vertices and
		// discard the path-critical convex ones. Under 3 vertices has no interior: IsPolygonCcw is
		// undefined there and the cross product below is zero, so such a polygon offers no candidates.
		bool bCcw = (iCount >= 3) && common::IsPolygonCcw(&pVertices[iStart], iCount);
		ASSERT(!bCcw);
		float fConvexSign = bCcw ? 1.0f : -1.0f;

		for (int32_t iLocal = 0; iLocal < iCount; ++iLocal)
		{
			int32_t iVertex = iStart + iLocal;
			vertexPolygon.at(static_cast<size_t>(iVertex)) = static_cast<int32_t>(iPoly);
			vertexLocal.at(static_cast<size_t>(iVertex)) = iLocal;
			vertexPolygonCount.at(static_cast<size_t>(iVertex)) = iCount;

			const XMFLOAT2& rPrevious = pVertices[iStart + (iLocal + iCount - 1) % iCount];
			const XMFLOAT2& rCurrent = pVertices[iVertex];
			const XMFLOAT2& rNext = pVertices[iStart + (iLocal + 1) % iCount];
			float fCross = (rCurrent.x - rPrevious.x) * (rNext.y - rCurrent.y) - (rCurrent.y - rPrevious.y) * (rNext.x - rCurrent.x);
			vertexConvex.at(static_cast<size_t>(iVertex)) = (fCross * fConvexSign) > 0.0f;
		}
	}

	for (int32_t i = 0; i < iVertexCount; ++i)
	{
		// Taut-string pruning: a shortest obstacle-avoiding path bends only at vertices convex on their own
		// obstacle, so every optimal path survives this restriction. Each polygon's full perimeter chain is
		// still added by BuildNavAdjacency, so dropping reflex vertices cannot disconnect the graph.
		if (!vertexConvex.at(static_cast<size_t>(i)))
		{
			continue;
		}

		for (int32_t j = i + 1; j < iVertexCount; ++j)
		{
			if (!vertexConvex.at(static_cast<size_t>(j)))
			{
				continue;
			}

			// Adjacent pairs on one polygon are perimeter edges; BuildNavAdjacency already supplies them.
			if (vertexPolygon.at(static_cast<size_t>(i)) == vertexPolygon.at(static_cast<size_t>(j)))
			{
				int32_t iLocalI = vertexLocal.at(static_cast<size_t>(i));
				int32_t iLocalJ = vertexLocal.at(static_cast<size_t>(j));
				int32_t iCount = vertexPolygonCount.at(static_cast<size_t>(i));
				if (iLocalJ - iLocalI == 1 || (iLocalI == 0 && iLocalJ == iCount - 1))
				{
					continue;
				}
			}

			XMFLOAT2 f2A = pVertices[i];
			XMFLOAT2 f2B = pVertices[j];

			// Blocked test first: it early-exits on its first crossing edge, which is the common case.
			if (SegmentBlockedByObstacle(f2A, f2B, pVertices, rNavData))
			{
				continue;
			}

			// A segment lying wholly inside a polygon crosses no edge, so the blocked test alone misses it.
			XMFLOAT2 f2Midpoint {(f2A.x + f2B.x) * 0.5f, (f2A.y + f2B.y) * 0.5f};
			if (PointInAnyPolygon(f2Midpoint, pVertices, rNavData))
			{
				continue;
			}

			rNavData.visEdgeA.push_back(i);
			rNavData.visEdgeB.push_back(j);
		}
	}
}

} // anonymous namespace

void BuildCellNavData(NavData& rNavData, const std::vector<IslandPlacement>& rPlacements)
{
	std::chrono::steady_clock::time_point startTimePoint = std::chrono::steady_clock::now();

	rNavData.vertices.clear();
	rNavData.polygonOffsets.clear();
	rNavData.visEdgeA.clear();
	rNavData.visEdgeB.clear();

	// Walk per-cell placements; each placement's template contour (UV-space) is rotated and
	// world-positioned around the placement's center. Topology offsets are rebased per island.
	for (const IslandPlacement& rPlacement : rPlacements)
	{
		const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(rPlacement.islandCrc);
		const NavContour& rContour = rTemplate.mNavContour;
		int32_t iVertexCount = static_cast<int32_t>(rContour.vertices.size());
		if (iVertexCount == 0)
		{
			continue;
		}

		int32_t iVertexBase = static_cast<int32_t>(rNavData.vertices.size());

		common::SinCos rotation = common::DeterministicSinCos(rPlacement.fRotation);
		float fCos = rotation.fCos;
		float fSin = rotation.fSin;
		float fFootprintX = rTemplate.mfQuadFootprintX;
		float fFootprintY = rTemplate.mfQuadFootprintY;

		// Local axes: +U = +world.x, +V = -world.y (V is world-Y inverted).
		for (int32_t i = 0; i < iVertexCount; ++i)
		{
			float fU = rContour.vertices.at(i).x;
			float fV = rContour.vertices.at(i).y;

			float fLocalX = (fU - 0.5f) * fFootprintX;
			float fLocalY = (0.5f - fV) * fFootprintY;

			float fRotX = fLocalX * fCos - fLocalY * fSin;
			float fRotY = fLocalX * fSin + fLocalY * fCos;

			rNavData.vertices.push_back({rPlacement.f2WorldPos.x + fRotX, rPlacement.f2WorldPos.y + fRotY});
		}

		for (int32_t iOffset : rContour.polygonOffsets)
		{
			rNavData.polygonOffsets.push_back(iVertexBase + iOffset);
		}
	}

	DebugCheckCrossingEdges(rNavData);

	// Acceleration first: the visibility pass queries SegmentBlockedByObstacle, which reads the
	// gridMin/gridMax and edge CSR built here. Its edges then need a second adjacency build — only
	// BuildNavAdjacency, not another full BuildNavAcceleration, because it rewrites adjOffsets and every
	// adjNeighbors slot outright.
	BuildNavAcceleration(rNavData);

	// An island-free cell keeps BuildNavAcceleration's cleared derived state, so the server ends where the
	// client's NavData::Read tail ends for that same cell instead of leaving a one-entry adjOffsets behind.
	if (!rNavData.vertices.empty())
	{
		BuildCellVisibilityGraph(rNavData);
		BuildNavAdjacency(rNavData);
	}

	LOG(kNavData, kInfo, "BuildCellNavData: placements={} vertices={} polygons={} visEdges={} wireBytes={} elapsedUs={}", rPlacements.size(), rNavData.vertices.size(), rNavData.polygonOffsets.size(), rNavData.visEdgeA.size(), rNavData.visEdgeA.size() * (sizeof(int32_t) + sizeof(int32_t)), std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTimePoint).count());
}

void BuildNavAcceleration(NavData& rNavData)
{
	rNavData.polygonMin.clear();
	rNavData.polygonMax.clear();
	rNavData.edgeA.clear();
	rNavData.edgeB.clear();
	rNavData.gridEdgeOffsets.clear();
	rNavData.gridEdges.clear();
	rNavData.adjOffsets.clear();
	rNavData.adjNeighbors.clear();
	rNavData.gridMin = {};
	rNavData.gridMax = {};

	int32_t iVertexCount = static_cast<int32_t>(rNavData.vertices.size());
	if (iVertexCount == 0)
	{
		return;
	}

	int32_t iPolygonCount = static_cast<int32_t>(rNavData.polygonOffsets.size());

	// --- Global vertex AABB (grid domain) ---
	float fMinX = std::numeric_limits<float>::max();
	float fMinY = std::numeric_limits<float>::max();
	float fMaxX = std::numeric_limits<float>::lowest();
	float fMaxY = std::numeric_limits<float>::lowest();
	for (const XMFLOAT2& rVertex : rNavData.vertices)
	{
		fMinX = std::min(fMinX, rVertex.x);
		fMinY = std::min(fMinY, rVertex.y);
		fMaxX = std::max(fMaxX, rVertex.x);
		fMaxY = std::max(fMaxY, rVertex.y);
	}
	rNavData.gridMin = {fMinX, fMinY};
	rNavData.gridMax = {fMaxX, fMaxY};

	// --- Per-polygon AABB + explicit perimeter edges ---
	rNavData.polygonMin.resize(iPolygonCount);
	rNavData.polygonMax.resize(iPolygonCount);
	for (int32_t iPoly = 0; iPoly < iPolygonCount; ++iPoly)
	{
		auto [iStart, iEnd] = PolygonRange(rNavData.polygonOffsets, iPoly, iVertexCount);
		int32_t iCount = iEnd - iStart;

		float fPolyMinX = std::numeric_limits<float>::max();
		float fPolyMinY = std::numeric_limits<float>::max();
		float fPolyMaxX = std::numeric_limits<float>::lowest();
		float fPolyMaxY = std::numeric_limits<float>::lowest();
		for (int32_t i = 0; i < iCount; ++i)
		{
			const XMFLOAT2& rVertex = rNavData.vertices.at(iStart + i);
			fPolyMinX = std::min(fPolyMinX, rVertex.x);
			fPolyMinY = std::min(fPolyMinY, rVertex.y);
			fPolyMaxX = std::max(fPolyMaxX, rVertex.x);
			fPolyMaxY = std::max(fPolyMaxY, rVertex.y);

			rNavData.edgeA.push_back(iStart + i);
			rNavData.edgeB.push_back(iStart + ((i + 1) % iCount));
		}
		rNavData.polygonMin.at(iPoly) = {fPolyMinX, fPolyMinY};
		rNavData.polygonMax.at(iPoly) = {fPolyMaxX, fPolyMaxY};
	}

	BuildNavEdgeGrid(rNavData);
	BuildNavAdjacency(rNavData);
}

void NavData::Write(std::ostream& rStream) const
{
	common::Write(rStream, static_cast<int32_t>(vertices.size()));
	for (const XMFLOAT2& rVertex : vertices)
	{
		common::Write(rStream, rVertex);
	}

	common::Write(rStream, static_cast<int32_t>(polygonOffsets.size()));
	for (int32_t iOffset : polygonOffsets)
	{
		common::Write(rStream, iOffset);
	}

	common::Write(rStream, static_cast<int32_t>(visEdgeA.size()));
	for (int32_t iEdge : visEdgeA)
	{
		common::Write(rStream, iEdge);
	}
	for (int32_t iEdge : visEdgeB)
	{
		common::Write(rStream, iEdge);
	}
}

void NavData::Read(std::istream& rStream)
{
	int32_t iVertexCount = 0;
	common::Read(rStream, iVertexCount);
	// Trust boundary (client static-data message; NavData rides there, not full-state): bound each count
	// against the stream before resize.
	common::ValidateDeserializedCount(iVertexCount, sizeof(XMFLOAT2), rStream, "NavData::Read vertices");
	vertices.resize(iVertexCount);
	for (int32_t i = 0; i < iVertexCount; ++i)
	{
		common::Read(rStream, vertices.at(i));
	}

	int32_t iPolygonCount = 0;
	common::Read(rStream, iPolygonCount);
	common::ValidateDeserializedCount(iPolygonCount, sizeof(int32_t), rStream, "NavData::Read polygons");
	polygonOffsets.resize(iPolygonCount);
	for (int32_t i = 0; i < iPolygonCount; ++i)
	{
		common::Read(rStream, polygonOffsets.at(i));
	}

	int32_t iEdgeCount = 0;
	common::Read(rStream, iEdgeCount);
	common::ValidateDeserializedCount(iEdgeCount, sizeof(int32_t) + sizeof(int32_t), rStream, "NavData::Read edges");
	visEdgeA.resize(iEdgeCount);
	visEdgeB.resize(iEdgeCount);
	for (int32_t i = 0; i < iEdgeCount; ++i)
	{
		common::Read(rStream, visEdgeA.at(i));
	}
	for (int32_t i = 0; i < iEdgeCount; ++i)
	{
		common::Read(rStream, visEdgeB.at(i));
	}

	// Derived broad-phase data is not serialized; rebuild it from the vertices just read so the client
	// matches the server's BuildCellNavData result.
	BuildNavAcceleration(*this);
}

} // namespace engine
