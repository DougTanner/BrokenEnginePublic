#include "NavBuild.h"

#include "Frame/IslandChainPlacement.h"
#include "Frame/IslandTerrain.h"

namespace engine
{

namespace
{

struct ContourEdge
{
	XMFLOAT2 f2A {};
	XMFLOAT2 f2B {};
	// Cell-edge identifiers for each endpoint. Each marching-squares midpoint lives on exactly one
	// cell-edge (horizontal or vertical); two cells that share a cell-edge produce the same midpoint
	// position and therefore the same identifier. Used as the chain-graph key in
	// ChainEdgesIntoPolygons, replacing the older `XMFLOAT2 → quantized uint64` keying that suffered
	// from single-precision float drift across the two sides of a shared edge.
	uint64_t uiKeyA = 0;
	uint64_t uiKeyB = 0;
};

// Encode (orientation, row, col) as a unique 64-bit cell-edge identifier. Horizontal edges have
// orient=0 with row ∈ [0, iHeight] / col ∈ [0, iWidth-1]; vertical edges have orient=1 with
// row ∈ [0, iHeight-1] / col ∈ [0, iWidth]. The 32+16+16 packing leaves comfortable headroom for
// the heightmap sizes the engine builds (kiElevationDivisor = 4 caps heightmap dims at a few
// thousand pixels).
inline constexpr uint64_t EncodeEdgeKey(uint32_t uiOrient, int32_t iRow, int32_t iCol)
{
	return (static_cast<uint64_t>(uiOrient) << 32) | (static_cast<uint64_t>(static_cast<uint32_t>(iRow)) << 16) | static_cast<uint64_t>(static_cast<uint32_t>(iCol));
}

// Marching squares: extract isocontour edges at the given world-space elevation threshold.
// Heightmap pixels are engine-meters relative to beach (0 == sea level) — sampled directly.
// Heightmap is anisotropic (DataPacker auto-crop produces non-square dims); UV scale is per-axis
// so the contour lives in [0, 1]² regardless of aspect ratio. Row stride is iWidth.
void ExtractContourEdges(std::vector<ContourEdge>& rEdges, const float* pfHeightmapData, int32_t iWidth, int32_t iHeight, float fWorldThreshold)
{
	float fScaleU = 1.0f / static_cast<float>(iWidth - 1);
	float fScaleV = 1.0f / static_cast<float>(iHeight - 1);

	for (int32_t iY = 0; iY < iHeight - 1; ++iY)
	{
		for (int32_t iX = 0; iX < iWidth - 1; ++iX)
		{
			float fTL = pfHeightmapData[iY * iWidth + iX];
			float fTR = pfHeightmapData[iY * iWidth + iX + 1];
			float fBR = pfHeightmapData[(iY + 1) * iWidth + iX + 1];
			float fBL = pfHeightmapData[(iY + 1) * iWidth + iX];

			// Classification: 1 = above threshold (obstacle), 0 = below (navigable)
			int32_t iCase = 0;
			if (fTL >= fWorldThreshold)
			{
				iCase |= 8;
			}
			if (fTR >= fWorldThreshold)
			{
				iCase |= 4;
			}
			if (fBR >= fWorldThreshold)
			{
				iCase |= 2;
			}
			if (fBL >= fWorldThreshold)
			{
				iCase |= 1;
			}

			if (iCase == 0 || iCase == 15)
			{
				continue;
			}

			// Interpolation helper: find the UV position where the contour crosses an edge
			float fCellU = static_cast<float>(iX) * fScaleU;
			float fCellV = static_cast<float>(iY) * fScaleV;
			float fStepU = fScaleU;
			float fStepV = fScaleV;

			// Edge midpoints via linear interpolation. Result is the unbounded crossing fraction in
			// [0, 1]; downstream chain keying uses the integer cell-edge identifier so float drift
			// or corner-snap quantization can no longer split a shared midpoint across two keys.
			auto Lerp = [](float fA, float fB, float fThresholdValue) -> float
			{
				float fDenom = fA - fB;
				if (std::abs(fDenom) < 1e-8f)
				{
					return 0.5f;
				}
				return std::clamp((fA - fThresholdValue) / fDenom, 0.0f, 1.0f);
			};

			const uint64_t uiKeyTop = EncodeEdgeKey(0, iY, iX);
			const uint64_t uiKeyRight = EncodeEdgeKey(1, iY, iX + 1);
			const uint64_t uiKeyBottom = EncodeEdgeKey(0, iY + 1, iX);
			const uint64_t uiKeyLeft = EncodeEdgeKey(1, iY, iX);

			// Top edge (TL to TR)
			float fTopT = Lerp(fTL, fTR, fWorldThreshold);
			XMFLOAT2 f2Top {fCellU + fTopT * fStepU, fCellV};

			// Right edge (TR to BR)
			float fRightT = Lerp(fTR, fBR, fWorldThreshold);
			XMFLOAT2 f2Right {fCellU + fStepU, fCellV + fRightT * fStepV};

			// Bottom edge (BL to BR)
			float fBottomT = Lerp(fBL, fBR, fWorldThreshold);
			XMFLOAT2 f2Bottom {fCellU + fBottomT * fStepU, fCellV + fStepV};

			// Left edge (TL to BL)
			float fLeftT = Lerp(fTL, fBL, fWorldThreshold);
			XMFLOAT2 f2Left {fCellU, fCellV + fLeftT * fStepV};

			// Produce edges based on case (standard marching squares lookup)
			// Cases 5 and 10 are saddle points: disambiguate by averaging corners
			switch (iCase)
			{
				case 1:
					rEdges.push_back({f2Bottom, f2Left, uiKeyBottom, uiKeyLeft});
					break;
				case 2:
					rEdges.push_back({f2Right, f2Bottom, uiKeyRight, uiKeyBottom});
					break;
				case 3:
					rEdges.push_back({f2Right, f2Left, uiKeyRight, uiKeyLeft});
					break;
				case 4:
					rEdges.push_back({f2Top, f2Right, uiKeyTop, uiKeyRight});
					break;
				case 5:
				{
					float fCenter = (fTL + fTR + fBR + fBL) * 0.25f;
					if (fCenter >= fWorldThreshold)
					{
						rEdges.push_back({f2Top, f2Left, uiKeyTop, uiKeyLeft});
						rEdges.push_back({f2Bottom, f2Right, uiKeyBottom, uiKeyRight});
					}
					else
					{
						rEdges.push_back({f2Top, f2Right, uiKeyTop, uiKeyRight});
						rEdges.push_back({f2Bottom, f2Left, uiKeyBottom, uiKeyLeft});
					}
					break;
				}
				case 6:
					rEdges.push_back({f2Top, f2Bottom, uiKeyTop, uiKeyBottom});
					break;
				case 7:
					rEdges.push_back({f2Top, f2Left, uiKeyTop, uiKeyLeft});
					break;
				case 8:
					rEdges.push_back({f2Left, f2Top, uiKeyLeft, uiKeyTop});
					break;
				case 9:
					rEdges.push_back({f2Bottom, f2Top, uiKeyBottom, uiKeyTop});
					break;
				case 10:
				{
					float fCenter = (fTL + fTR + fBR + fBL) * 0.25f;
					if (fCenter >= fWorldThreshold)
					{
						rEdges.push_back({f2Left, f2Bottom, uiKeyLeft, uiKeyBottom});
						rEdges.push_back({f2Right, f2Top, uiKeyRight, uiKeyTop});
					}
					else
					{
						rEdges.push_back({f2Left, f2Top, uiKeyLeft, uiKeyTop});
						rEdges.push_back({f2Right, f2Bottom, uiKeyRight, uiKeyBottom});
					}
					break;
				}
				case 11:
					rEdges.push_back({f2Right, f2Top, uiKeyRight, uiKeyTop});
					break;
				case 12:
					rEdges.push_back({f2Left, f2Right, uiKeyLeft, uiKeyRight});
					break;
				case 13:
					rEdges.push_back({f2Bottom, f2Right, uiKeyBottom, uiKeyRight});
					break;
				case 14:
					rEdges.push_back({f2Left, f2Bottom, uiKeyLeft, uiKeyBottom});
					break;
				default:
					break;
			}
		}
	}
}

// Chain contour edges into closed polygons by matching endpoints
void ChainEdgesIntoPolygons(std::vector<std::vector<XMFLOAT2>>& rPolygons, const std::vector<ContourEdge>& rEdges)
{
	// Adjacency: each cell-edge identifier maps to the edges that touch it. Because each midpoint
	// lives on exactly one cell-edge and is shared with exactly one neighbour cell, every key has
	// exactly two entries (one per cell on either side of the edge) — no quantization-induced
	// false T-junctions.
	struct EdgeRef
	{
		size_t iEdgeIndex;
		bool bIsEndpointB; // false = matched on f2A / uiKeyA, true = matched on f2B / uiKeyB
	};
	std::unordered_multimap<uint64_t, EdgeRef> vertexToEdge;
	vertexToEdge.reserve(rEdges.size() * 2);
	for (size_t i = 0; i < rEdges.size(); ++i)
	{
		vertexToEdge.insert({rEdges.at(i).uiKeyA, {i, false}});
		vertexToEdge.insert({rEdges.at(i).uiKeyB, {i, true}});

		// Determinism guard: each cell-edge key is shared by at most two cells (one entry per side), so
		// the chain walk's unused-candidate pick is unique regardless of bucket order. A future
		// ExtractContourEdges change emitting a duplicate key would make NavData hash-bucket-order-
		// dependent (divergent across builds). At-most-two, not exactly-two: boundary cell-edges have one.
		ASSERT(vertexToEdge.count(rEdges.at(i).uiKeyA) <= 2);
		ASSERT(vertexToEdge.count(rEdges.at(i).uiKeyB) <= 2);
	}

	std::vector<bool> used(rEdges.size(), false);

	for (size_t i = 0; i < rEdges.size(); ++i)
	{
		if (used.at(i))
		{
			continue;
		}

		std::vector<XMFLOAT2> polygon;
		polygon.push_back(rEdges.at(i).f2A);
		polygon.push_back(rEdges.at(i).f2B);
		used.at(i) = true;
		const uint64_t uiHeadKey = rEdges.at(i).uiKeyA;
		uint64_t uiTailKey = rEdges.at(i).uiKeyB;

		bool bGrowing = true;
		while (bGrowing)
		{
			bGrowing = false;
			auto range = vertexToEdge.equal_range(uiTailKey);
			for (auto it = range.first; it != range.second; ++it)
			{
				size_t j = it->second.iEdgeIndex;
				if (used.at(j))
				{
					continue;
				}

				if (it->second.bIsEndpointB)
				{
					polygon.push_back(rEdges.at(j).f2A);
					uiTailKey = rEdges.at(j).uiKeyA;
				}
				else
				{
					polygon.push_back(rEdges.at(j).f2B);
					uiTailKey = rEdges.at(j).uiKeyB;
				}
				used.at(j) = true;
				bGrowing = true;
				break;
			}
		}

		// Closed iff the chain returned to the head's cell-edge. Integer-exact compare — no
		// epsilon needed, since cell-edge keys are derived from cell indices, not float positions.
		if (polygon.size() >= 3 && uiTailKey == uiHeadKey)
		{
			polygon.pop_back();
			rPolygons.push_back(std::move(polygon));
		}
	}
}

// Segment-segment intersection test
// Returns true if segments (A1,A2) and (B1,B2) intersect (proper intersection only, not endpoint touching)
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

// Point-in-polygon test using winding number
bool PointInPolygon(XMFLOAT2 f2Point, const XMFLOAT2* pVertices, int32_t iVertexCount)
{
	int32_t iWinding = 0;
	for (int32_t i = 0; i < iVertexCount; ++i)
	{
		int32_t iNext = (i + 1) % iVertexCount;
		XMFLOAT2 f2A = pVertices[i];
		XMFLOAT2 f2B = pVertices[iNext];

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
	return iWinding != 0;
}

// Check if a segment intersects any polygon edge in the NavData
bool SegmentIntersectsAnyEdge(XMFLOAT2 f2A, XMFLOAT2 f2B, const std::vector<XMFLOAT2>& rVertices, const std::vector<int32_t>& rPolygonOffsets)
{
	for (size_t iPoly = 0; iPoly < rPolygonOffsets.size(); ++iPoly)
	{
		int32_t iStart = rPolygonOffsets.at(iPoly);
		int32_t iEnd = (iPoly + 1 < rPolygonOffsets.size()) ? rPolygonOffsets.at(iPoly + 1) : static_cast<int32_t>(rVertices.size());
		int32_t iCount = iEnd - iStart;

		for (int32_t i = 0; i < iCount; ++i)
		{
			int32_t iNext = (i + 1) % iCount;
			if (SegmentsIntersect(f2A, f2B, rVertices.at(iStart + i), rVertices.at(iStart + iNext)))
			{
				return true;
			}
		}
	}
	return false;
}

// Check if the midpoint of a segment is inside any obstacle polygon
bool MidpointInsideObstacle(XMFLOAT2 f2A, XMFLOAT2 f2B, const std::vector<XMFLOAT2>& rVertices, const std::vector<int32_t>& rPolygonOffsets)
{
	XMFLOAT2 f2Mid {(f2A.x + f2B.x) * 0.5f, (f2A.y + f2B.y) * 0.5f};

	for (size_t iPoly = 0; iPoly < rPolygonOffsets.size(); ++iPoly)
	{
		int32_t iStart = rPolygonOffsets.at(iPoly);
		int32_t iEnd = (iPoly + 1 < rPolygonOffsets.size()) ? rPolygonOffsets.at(iPoly + 1) : static_cast<int32_t>(rVertices.size());
		int32_t iCount = iEnd - iStart;

		if (PointInPolygon(f2Mid, &rVertices.at(iStart), iCount))
		{
			return true;
		}
	}
	return false;
}


void BuildVisibilityGraph(NavContour& rContour)
{
	int32_t iVertexCount = static_cast<int32_t>(rContour.vertices.size());

	for (int32_t i = 0; i < iVertexCount; ++i)
	{
		for (int32_t j = i + 1; j < iVertexCount; ++j)
		{
			// Skip edges between adjacent vertices on the same polygon (they're polygon edges, not visibility edges)
			bool bAdjacent = false;
			for (size_t iPoly = 0; iPoly < rContour.polygonOffsets.size(); ++iPoly)
			{
				int32_t iStart = rContour.polygonOffsets.at(iPoly);
				int32_t iEnd = (iPoly + 1 < rContour.polygonOffsets.size()) ? rContour.polygonOffsets.at(iPoly + 1) : iVertexCount;
				int32_t iCount = iEnd - iStart;

				if (i >= iStart && i < iEnd && j >= iStart && j < iEnd)
				{
					int32_t iLocalI = i - iStart;
					int32_t iLocalJ = j - iStart;
					if (iLocalJ - iLocalI == 1 || (iLocalI == 0 && iLocalJ == iCount - 1))
					{
						bAdjacent = true;
						break;
					}
				}
			}

			if (bAdjacent)
			{
				continue;
			}

			XMFLOAT2 f2A = rContour.vertices.at(i);
			XMFLOAT2 f2B = rContour.vertices.at(j);

			if (SegmentIntersectsAnyEdge(f2A, f2B, rContour.vertices, rContour.polygonOffsets))
			{
				continue;
			}

			if (MidpointInsideObstacle(f2A, f2B, rContour.vertices, rContour.polygonOffsets))
			{
				continue;
			}

			rContour.visEdgeA.push_back(i);
			rContour.visEdgeB.push_back(j);
		}
	}
}

} // anonymous namespace

void BuildNavContour(NavContour& rContour, const float* pfHeightmapData, int32_t iHeightmapWidth, int32_t iHeightmapHeight, float fWorldThreshold)
{
	LOG(kNavData, kDebug, "NavBuild: heightmap {}x{} worldThreshold={}", iHeightmapWidth, iHeightmapHeight, common::Wb(fWorldThreshold, 4));

	// Step 1: Extract contour edges via marching squares
	std::vector<ContourEdge> contourEdges;
	contourEdges.reserve(static_cast<size_t>(iHeightmapWidth) * static_cast<size_t>(iHeightmapHeight));
	ExtractContourEdges(contourEdges, pfHeightmapData, iHeightmapWidth, iHeightmapHeight, fWorldThreshold);

	LOG(kNavData, kDebug, "NavBuild: extracted {} contour edges", contourEdges.size());

	if (contourEdges.empty())
	{
		return;
	}

	// Step 2: Chain edges into closed polygons
	std::vector<std::vector<XMFLOAT2>> polygons;
	ChainEdgesIntoPolygons(polygons, contourEdges);

	LOG(kNavData, kDebug, "NavBuild: chained into {} polygons", polygons.size());

	// Step 3: Hand raw chained polygons to Clipper2 — Union+Inflate+Simplify in UV space.
	// Clipper2 is Vatti-based with integer-coordinate robustness; miter offsets fall back to
	// bevel at concave corners that would otherwise self-intersect (the bowtie failure mode
	// the previous custom miter offset suffered from).
	static constexpr double kfInflateDelta = 0.015;
	static constexpr double kfMiterLimit = 2.0;
	static constexpr double kfSimplifyEpsilon = 0.005;
	// Clipper2 PathsD quantizes doubles to int64 at 10^precision per unit. Default 2 gives a
	// 0.01-UV grid (~1 world unit per cell) which snaps diagonals into right-angle staircases.
	// 6 gives sub-micrometer UV granularity — plenty for the marching-squares input.
	static constexpr int kiClipperPrecision = 6;

	Clipper2Lib::PathsD obstacles;
	obstacles.reserve(polygons.size());
	for (const std::vector<XMFLOAT2>& rPoly : polygons)
	{
		if (rPoly.size() < 3)
		{
			continue;
		}
		Clipper2Lib::PathD path;
		path.reserve(rPoly.size());
		for (const XMFLOAT2& rVert : rPoly)
		{
			path.emplace_back(rVert.x, rVert.y);
		}
		obstacles.push_back(std::move(path));
	}

	// Union resolves overlaps; positive winding = outer obstacle, negative = enclosed hole.
	Clipper2Lib::PathsD unioned = Clipper2Lib::Union(obstacles, Clipper2Lib::FillRule::NonZero, kiClipperPrecision);
	// Outward miter offset; Clipper2 auto-bevels when miter would exceed limit.
	Clipper2Lib::PathsD inflated = Clipper2Lib::InflatePaths(unioned, kfInflateDelta, Clipper2Lib::JoinType::Miter, Clipper2Lib::EndType::Polygon, kfMiterLimit, kiClipperPrecision);
	// Topology-preserving simplification (does not introduce crossings).
	Clipper2Lib::PathsD simplified = Clipper2Lib::SimplifyPaths(inflated, kfSimplifyEpsilon);

	// Step 4: Pack outer obstacle loops into flat arrays. Discard holes (Area < 0): they sit
	// inside obstacles and the visibility graph cannot route through obstacles regardless.
	int32_t iDroppedHoles = 0;
	for (const Clipper2Lib::PathD& rPath : simplified)
	{
		if (rPath.size() < 3)
		{
			continue;
		}
		if (Clipper2Lib::Area(rPath) <= 0.0)
		{
			++iDroppedHoles;
			continue;
		}

		rContour.polygonOffsets.push_back(static_cast<int32_t>(rContour.vertices.size()));
		for (const Clipper2Lib::PointD& rPt : rPath)
		{
			rContour.vertices.push_back({static_cast<float>(rPt.x), static_cast<float>(rPt.y)});
		}
	}

	if (iDroppedHoles > 0)
	{
		LOG(kNavData, kDebug, "NavBuild: dropped {} interior holes", iDroppedHoles);
	}

	LOG(kNavData, kDebug, "NavBuild: total vertices={} polygons={}", rContour.vertices.size(), rContour.polygonOffsets.size());

	if (rContour.vertices.empty())
	{
		return;
	}

	// PointInPolygon's winding-number test assumes consistent CCW winding per polygon. Clipper2 filters
	// by double-precision Area > 0 above; this verifies the float-cast vertices still satisfy the
	// invariant so a near-degenerate truncation can't silently misclassify obstacle interiors.
	int32_t iPolyCount = static_cast<int32_t>(rContour.polygonOffsets.size());
	int32_t iVertexTotal = static_cast<int32_t>(rContour.vertices.size());
	for (int32_t iPoly = 0; iPoly < iPolyCount; ++iPoly)
	{
		int32_t iStart = rContour.polygonOffsets.at(iPoly);
		int32_t iEnd = (iPoly + 1 < iPolyCount) ? rContour.polygonOffsets.at(iPoly + 1) : iVertexTotal;
		int32_t iCount = iEnd - iStart;
		if (iCount < 3)
		{
			continue;
		}
		float fSignedArea = 0.0f;
		for (int32_t i = 0; i < iCount; ++i)
		{
			XMFLOAT2 f2A = rContour.vertices.at(iStart + i);
			XMFLOAT2 f2B = rContour.vertices.at(iStart + (i + 1) % iCount);
			fSignedArea += f2A.x * f2B.y - f2B.x * f2A.y;
		}
		ASSERT(fSignedArea > 0.0f);
	}

	// Step 5: Build visibility graph
	BuildVisibilityGraph(rContour);

	LOG(kNavData, kDebug, "NavBuild: visibility graph edges={}", rContour.visEdgeA.size());
}

void BuildCellNavData(NavData& rNavData, const std::vector<IslandPlacement>& rPlacements)
{
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

		for (int32_t iEdge : rContour.visEdgeA)
		{
			rNavData.visEdgeA.push_back(iVertexBase + iEdge);
		}
		for (int32_t iEdge : rContour.visEdgeB)
		{
			rNavData.visEdgeB.push_back(iVertexBase + iEdge);
		}
	}

	// Detect crossing polygon edges (the yellow debug lines). Two non-adjacent polygon
	// edges that properly intersect indicate self-intersecting contour or overlapping
	// placements — either way the visibility graph + pathfinder will misbehave.
	// Pure diagnostic: O(edges^2) double loop, compiled out by default. Flip
	// kbDebugNavCrossingCheck to true locally when investigating a suspected contour defect.
	if constexpr (kbDebugNavCrossingCheck)
	{
		int32_t iVertexCount = static_cast<int32_t>(rNavData.vertices.size());
		int32_t iPolygonCount = static_cast<int32_t>(rNavData.polygonOffsets.size());
		for (int32_t iPolyA = 0; iPolyA < iPolygonCount; ++iPolyA)
		{
			int32_t iStartA = rNavData.polygonOffsets.at(iPolyA);
			int32_t iEndA = (iPolyA + 1 < iPolygonCount) ? rNavData.polygonOffsets.at(iPolyA + 1) : iVertexCount;
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
					int32_t iStartB = rNavData.polygonOffsets.at(iPolyB);
					int32_t iEndB = (iPolyB + 1 < iPolygonCount) ? rNavData.polygonOffsets.at(iPolyB + 1) : iVertexCount;
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

	BuildNavAcceleration(rNavData);
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
		int32_t iStart = rNavData.polygonOffsets.at(iPoly);
		int32_t iEnd = (iPoly + 1 < iPolygonCount) ? rNavData.polygonOffsets.at(iPoly + 1) : iVertexCount;
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

	// --- Edge grid (CSR): bucket each edge into every cell its AABB overlaps (conservative) ---
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

	// --- Per-vertex adjacency CSR (visibility-graph neighbors + polygon prev/next) ---
	int32_t iVisCount = static_cast<int32_t>(rNavData.visEdgeA.size());
	std::vector<int32_t> adjCounts(static_cast<size_t>(iVertexCount), 0);
	for (int32_t iVis = 0; iVis < iVisCount; ++iVis)
	{
		++adjCounts.at(static_cast<size_t>(rNavData.visEdgeA.at(iVis)));
		++adjCounts.at(static_cast<size_t>(rNavData.visEdgeB.at(iVis)));
	}
	for (int32_t iPoly = 0; iPoly < iPolygonCount; ++iPoly)
	{
		int32_t iStart = rNavData.polygonOffsets.at(iPoly);
		int32_t iEnd = (iPoly + 1 < iPolygonCount) ? rNavData.polygonOffsets.at(iPoly + 1) : iVertexCount;
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
		int32_t iStart = rNavData.polygonOffsets.at(iPoly);
		int32_t iEnd = (iPoly + 1 < iPolygonCount) ? rNavData.polygonOffsets.at(iPoly + 1) : iVertexCount;
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
	vertices.resize(iVertexCount);
	for (int32_t i = 0; i < iVertexCount; ++i)
	{
		common::Read(rStream, vertices.at(i));
	}

	int32_t iPolygonCount = 0;
	common::Read(rStream, iPolygonCount);
	polygonOffsets.resize(iPolygonCount);
	for (int32_t i = 0; i < iPolygonCount; ++i)
	{
		common::Read(rStream, polygonOffsets.at(i));
	}

	int32_t iEdgeCount = 0;
	common::Read(rStream, iEdgeCount);
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
