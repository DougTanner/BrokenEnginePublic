#pragma once

namespace engine
{

struct IslandPlacement;

// Canonical island contour in UV space [0,1]x[0,1], built once from heightmap
struct NavContour
{
	std::vector<XMFLOAT2> vertices;
	std::vector<int32_t> polygonOffsets;
};

inline constexpr int64_t kiNavDataVersion = 14;

// Broad-phase edge grid dimensions (tunable). Mirrors the fixed-zone grid in Collision.h; finer here
// because obstacle edges are denser than collision objects. Only affects derived (non-serialized) data.
inline constexpr int32_t kiNavZonesX = 64;
inline constexpr int32_t kiNavZonesY = 64;

// Map a world coordinate to a nav-grid cell index on one axis. Shared by the builder and the query so
// bucketing and lookup always agree. Degenerate (near-zero) extent collapses to cell 0.
inline int32_t NavGridCell(float fValue, float fMin, float fMax, int32_t iZones)
{
	float fExtent = fMax - fMin;
	if (fExtent <= 1e-6f)
	{
		return 0;
	}
	int32_t iCell = static_cast<int32_t>((fValue - fMin) / fExtent * static_cast<float>(iZones));
	return std::clamp(iCell, 0, iZones - 1);
}

// Per-cell navigation data in world space, stored in FrameStaticData
struct NavData
{
	// --- Serialized (logical) content. Bump kiNavDataVersion if this layout changes. visEdgeA/visEdgeB
	// are the whole-cell world-space visibility graph BuildCellNavData computes over every polygon in the
	// cell (server-built, shipped to clients); templates carry contour geometry only. ---
	std::vector<XMFLOAT2> vertices;
	std::vector<int32_t> polygonOffsets;
	std::vector<int32_t> visEdgeA;
	std::vector<int32_t> visEdgeB;

	// --- Derived broad-phase acceleration, rebuilt by BuildNavAcceleration from the vertices above on
	// both sides (server: end of BuildCellNavData; client/load: end of Read). NOT serialized, so it never
	// affects kiNavDataVersion. ---
	std::vector<XMFLOAT2> polygonMin;     // per polygon: AABB min (xy)
	std::vector<XMFLOAT2> polygonMax;     // per polygon: AABB max (xy)
	std::vector<int32_t> edgeA;           // obstacle perimeter edges: endpoint vertex indices
	std::vector<int32_t> edgeB;
	std::vector<int32_t> gridEdgeOffsets; // CSR offsets, size kiNavZonesX*kiNavZonesY + 1
	std::vector<int32_t> gridEdges;       // CSR payload: edge indices bucketed per grid cell
	std::vector<int32_t> adjOffsets;      // CSR offsets per vertex, size vertices.size() + 1
	std::vector<int32_t> adjNeighbors;    // CSR payload: neighbor vertex indices (visibility + polygon)
	XMFLOAT2 gridMin {};                  // global vertex AABB min (grid origin)
	XMFLOAT2 gridMax {};                  // global vertex AABB max

	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
};

void BuildNavContour(NavContour& rContour, const float* pfHeightmapData, int32_t iHeightmapWidth, int32_t iHeightmapHeight, float fWorldThreshold, float fFootprintXMeters, float fFootprintYMeters);
void BuildCellNavData(NavData& rNavData, const std::vector<IslandPlacement>& rPlacements);

// Rebuild the derived broad-phase acceleration (polygon AABBs, explicit edges, edge grid, per-vertex
// adjacency) from the serialized vertices/polygons/visibility edges. Deterministic — both client and
// server derive identical structures from identical input.
void BuildNavAcceleration(NavData& rNavData);

} // namespace engine
