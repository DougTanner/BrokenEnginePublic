#pragma once

// Private (non-public) header shared by the two NavBuild translation units — NavBuild.cpp
// (contour-build domain) and NavCellData.cpp (cell-merge / acceleration / serialization domain).
// Holds only the single geometry predicate that crosses the domain boundary: SegmentsIntersect is
// called by SegmentIntersectsAnyEdge in NavBuild.cpp AND by the kbDebugNavCrossingCheck diagnostic in
// NavCellData.cpp's BuildCellNavData, so it is promoted from file-local to external linkage and
// forward-declared here (its definition lives in NavBuild.cpp). NOT part of the public API (NavBuild.h).

namespace engine
{

// Returns true if segments (A1,A2) and (B1,B2) properly intersect (interior crossing only, not
// endpoint touching). Defined in NavBuild.cpp.
bool SegmentsIntersect(XMFLOAT2 f2A1, XMFLOAT2 f2A2, XMFLOAT2 f2B1, XMFLOAT2 f2B2);

} // namespace engine
