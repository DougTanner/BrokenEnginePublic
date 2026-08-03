#include "NavBuild.h"

#include "NavBuildInternal.h"

#include "Frame/Collections/Players/Players.h"

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

} // anonymous namespace

// Segment-segment intersection test. Promoted from the anonymous namespace to external linkage
// (engine::, declared in NavBuildInternal.h) so NavCellData.cpp's kbDebugNavCrossingCheck diagnostic
// can call it across the TU boundary. Proper interior crossing only (not endpoint touching).
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

// Winding-number point-in-polygon test. Promoted from the anonymous namespace to external linkage
// (engine::, declared in NavBuildInternal.h) so NavQuery.cpp's PointInAnyPolygon shares one winding
// core with the builder — a tuned boundary rule can't drift between build and query. Pointer + count
// so callers can pass a sub-range of a larger vertex buffer (PointInAnyPolygon).
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

void BuildNavContour(NavContour& rContour, const float* pfHeightmapData, int32_t iHeightmapWidth, int32_t iHeightmapHeight, float fWorldThreshold, float fFootprintXMeters, float fFootprintYMeters)
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

	// Step 3: Hand raw chained polygons to Clipper2 — Union in UV, then transform to centered local
	// metric coordinates for isotropic inflation and simplification before mapping back to UV.
	// Clipper2 is Vatti-based with integer-coordinate robustness; miter offsets fall back to
	// bevel at concave corners that would otherwise self-intersect (the bowtie failure mode
	// the previous custom miter offset suffered from).
	static constexpr double kfClearanceMeters = game::kfPlayerRadius + game::kfPushMargin;
	static constexpr double kfMiterLimit = 2.0;
	static constexpr double kfSimplifyEpsilonMeters = 1.00;
	// Clipper2 PathsD quantizes doubles to int64 at 10^precision per unit. Keep precision 6 for both
	// the UV union and metric offset so marching-squares detail and meter-scale clearance remain stable.
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
	for (Clipper2Lib::PathD& rPath : unioned)
	{
		for (Clipper2Lib::PointD& rPoint : rPath)
		{
			rPoint.x = (rPoint.x - 0.5) * static_cast<double>(fFootprintXMeters);
			rPoint.y = (rPoint.y - 0.5) * static_cast<double>(fFootprintYMeters);
		}
	}

	// The nav polygon is deliberately the terrain-push contour plus ship clearance. All new or changed
	// distances and tolerances here are meters; UV is representation only.
	// Outward miter offset; Clipper2 auto-bevels when miter would exceed limit.
	Clipper2Lib::PathsD inflated = Clipper2Lib::InflatePaths(unioned, kfClearanceMeters, Clipper2Lib::JoinType::Miter, Clipper2Lib::EndType::Polygon, kfMiterLimit, kiClipperPrecision);
	// Topology-preserving simplification (does not introduce crossings).
	Clipper2Lib::PathsD simplified = Clipper2Lib::SimplifyPaths(inflated, kfSimplifyEpsilonMeters);

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
		for (const Clipper2Lib::PointD& rPoint : rPath)
		{
			float fU = static_cast<float>(rPoint.x / static_cast<double>(fFootprintXMeters) + 0.5);
			float fV = static_cast<float>(rPoint.y / static_cast<double>(fFootprintYMeters) + 0.5);
			rContour.vertices.push_back(
			{
				fU,
				fV,
			});
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

	// Clipper2's Area > 0 filter above kept only outer obstacle loops; this verifies the float-cast
	// vertices still wind the same way, so a near-degenerate truncation can't silently flip one into a
	// hole. Not a PointInPolygon precondition — that test counts nonzero winding and is orientation-
	// agnostic. The invariant is UV-space only: BuildCellNavData mirrors Y when it places a template, so
	// the merged world-space polygons are wound clockwise.
	int32_t iPolyCount = static_cast<int32_t>(rContour.polygonOffsets.size());
	int32_t iVertexTotal = static_cast<int32_t>(rContour.vertices.size());
	for (int32_t iPoly = 0; iPoly < iPolyCount; ++iPoly)
	{
		auto [iStart, iEnd] = PolygonRange(rContour.polygonOffsets, iPoly, iVertexTotal);
		int32_t iCount = iEnd - iStart;
		if (iCount < 3)
		{
			continue;
		}
		ASSERT(common::IsPolygonCcw(&rContour.vertices.at(iStart), iCount));
	}
}

} // namespace engine
