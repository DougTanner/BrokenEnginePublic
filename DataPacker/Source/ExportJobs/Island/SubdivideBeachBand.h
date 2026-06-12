#pragma once

// Tuning knobs for SubdivideBeachBand. Bundled so the function signature stays short.
struct SubdivisionConfig
{
	float fBandMinMeters;     // Triangles whose Z-range overlaps [fBandMinMeters, fBandMaxMeters] are candidates
	float fBandMaxMeters;
	float fMaxEdgeMeters;     // In-band triangles subdivide until longest XY edge <= this
	int32_t iMaxDepth;        // Safety cap on recursive subdivision depth
};

// Adaptive beach-band mesh subdivision pass invoked by BakeIslandIntermediates after the Gaea
// Mesher glTF is loaded. Triangles whose Z-range overlaps [fBandMinMeters, fBandMaxMeters]
// (straddling beach Z=0) are recursively midpoint-subdivided until their longest XY edge is
// <= fMaxEdgeMeters; out-of-band one-ring neighbors absorb the resulting midpoints via minimal
// 1->2 / 1->3 / 1->4 splits that introduce no new midpoints (cascade firewall). Triangles that
// would exceed iMaxDepth are skipped and riDepthCapHits is incremented (caller logs a warning).
// Output appends new vertices/indices to the same vectors; the dead-triangle compaction at the
// end ensures the index buffer contains no UINT32_MAX sentinels on return. iVertexCount and
// iIndexCount must be recomputed by the caller from vector sizes after this returns.
void SubdivideBeachBand(std::vector<float>& rMeshPositions, std::vector<uint32_t>& rMeshIndices, const SubdivisionConfig& rConfig, int64_t& riDepthCapHits);
