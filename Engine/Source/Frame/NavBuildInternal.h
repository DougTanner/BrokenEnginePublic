#pragma once

// Private (non-public) header shared by the nav translation units — NavBuild.cpp (contour-build
// domain), NavCellData.cpp (cell-merge / acceleration / serialization domain), and NavQuery.cpp
// (runtime LOS / A* query domain). Holds the geometry predicates the build path and the query path
// must share so a tuned epsilon or boundary rule can never drift between them — drift would let the
// build-time visibility graph and the runtime query disagree, threading paths through walls. Both
// predicates are promoted from file-local to external linkage (definitions live in NavBuild.cpp).
// NOT part of the public API (NavBuild.h).

namespace engine
{

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

} // namespace engine
