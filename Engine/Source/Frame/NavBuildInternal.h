#pragma once

// Private (non-public) header shared by the nav translation units — NavBuild.cpp (contour-build
// domain), NavCellData.cpp (cell-merge / acceleration / serialization domain), and NavQuery.cpp
// (runtime LOS / A* query domain). Holds the geometry predicates the build path and the query path
// must share so a tuned epsilon or boundary rule can never drift between them — drift would let the
// build-time visibility graph and the runtime query disagree, threading paths through walls. Each
// predicate is promoted from file-local to external linkage; its definition stays in the TU that owns
// its domain, named per declaration below. NOT part of the public API (NavBuild.h).

namespace engine
{

struct NavData;

template <typename T>
inline std::pair<int32_t, int32_t> PolygonRange(const std::vector<int32_t>& rPolygonOffsets, T polygonIndex, int32_t iVertexTotal)
{
	int32_t iStart = rPolygonOffsets.at(static_cast<size_t>(polygonIndex));
	int32_t iEnd = (polygonIndex + 1 < static_cast<T>(rPolygonOffsets.size())) ? rPolygonOffsets.at(static_cast<size_t>(polygonIndex + 1)) : iVertexTotal;
	return {iStart, iEnd};
}

// Returns true if segments (A1,A2) and (B1,B2) properly intersect (interior crossing only, not
// endpoint touching). Defined in NavBuild.cpp.
bool SegmentsIntersect(XMFLOAT2 f2A1, XMFLOAT2 f2A2, XMFLOAT2 f2B1, XMFLOAT2 f2B2);

// Winding-number point-in-polygon test over one polygon's vertices (pointer + count, so callers can
// pass a sub-range of a larger vertex buffer). Defined in NavBuild.cpp.
bool PointInPolygon(XMFLOAT2 f2Point, const XMFLOAT2* pVertices, int32_t iVertexCount);

// Does the segment cross any obstacle edge? Grid-DDA broad phase over the NavData edge CSR. Defined in
// NavQuery.cpp; the whole-cell visibility build in NavCellData.cpp shares it so an edge exists in the
// graph iff the runtime blocked test says the segment is clear.
bool SegmentBlockedByObstacle(XMFLOAT2 f2A, XMFLOAT2 f2B, const XMFLOAT2* pVertices, const NavData& rNavData);

// Is the point inside any obstacle polygon? Per-polygon AABB broad phase over PointInPolygon. Defined
// in NavQuery.cpp; shared with the same visibility build.
bool PointInAnyPolygon(XMFLOAT2 f2Point, const XMFLOAT2* pVertices, const NavData& rNavData);

} // namespace engine
