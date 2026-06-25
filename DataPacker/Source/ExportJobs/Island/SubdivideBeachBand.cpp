#include "SubdivideBeachBand.h"

namespace
{

// All lambdas in the original inline block captured the same shared mutable state by reference
// (the two mesh buffers, the worklist, the per-triangle alive/depth vectors, and the two edge
// adjacency maps). Lifting them into a struct makes the dependency graph explicit: refs that
// outlive Run() are constructor inputs, internal state is direct members. Run() drives the
// worklist; everything else is a helper. TU-local in an anonymous namespace.
struct BeachSubdivider
{
	BeachSubdivider(std::vector<float>& rMeshPositions, std::vector<uint32_t>& rMeshIndices, const SubdivisionConfig& rConfig, int64_t& riDepthCapHits)
		: meshPositions(rMeshPositions)
		, meshIndices(rMeshIndices)
		, fBandMinZ(rConfig.fBandMinMeters)
		, fBandMaxZ(rConfig.fBandMaxMeters)
		, fMaxEdge(rConfig.fMaxEdgeMeters)
		, iMaxDepth(rConfig.iMaxDepth)
		, riDepthCapHits(riDepthCapHits)
	{
	}

	// Edge -> the (up to) two triangles sharing it. -1 marks an empty slot.
	struct EdgeSlots
	{
		int32_t iSlot0 = -1;
		int32_t iSlot1 = -1;
	};

	void Run();
	void SplitInBand(uint32_t iTriangle, uint32_t iA, uint32_t iB, uint32_t iC, uint32_t iMidpointAB, uint32_t iMidpointBC, uint32_t iMidpointCA, uint8_t iChildDepth);
	void AbsorbMidpoints(uint32_t iTriangle, uint32_t iA, uint32_t iB, uint32_t iC, uint32_t iMidpointAB, uint32_t iMidpointBC, uint32_t iMidpointCA, int32_t iExistingMidpoints, uint8_t iChildDepth);

	uint64_t EdgeKey(uint32_t iA, uint32_t iB) const
	{
		uint32_t iMin = std::min(iA, iB);
		uint32_t iMax = std::max(iA, iB);
		return (static_cast<uint64_t>(iMin) << 32) | static_cast<uint64_t>(iMax);
	}

	float VertexX(uint32_t iV) const
	{
		return meshPositions[static_cast<size_t>(iV) * 3 + 0];
	}
	float VertexY(uint32_t iV) const
	{
		return meshPositions[static_cast<size_t>(iV) * 3 + 1];
	}
	float VertexZ(uint32_t iV) const
	{
		return meshPositions[static_cast<size_t>(iV) * 3 + 2];
	}

	void EdgeAdd(uint64_t iKey, int32_t iTriangle)
	{
		EdgeSlots& rSlots = edgeTriangles.try_emplace(iKey, EdgeSlots {}).first->second;
		if (rSlots.iSlot0 < 0)
		{
			rSlots.iSlot0 = iTriangle;
			return;
		}
		if (rSlots.iSlot1 < 0)
		{
			rSlots.iSlot1 = iTriangle;
			return;
		}
		ASSERT(false);  // 3+ triangles share a single edge -- malformed mesh input.
	}

	void EdgeRemove(uint64_t iKey, int32_t iTriangle)
	{
		auto it = edgeTriangles.find(iKey);
		if (it == edgeTriangles.end())
		{
			return;
		}
		EdgeSlots& rSlots = it->second;
		if (rSlots.iSlot0 == iTriangle)
		{
			rSlots.iSlot0 = -1;
		}
		if (rSlots.iSlot1 == iTriangle)
		{
			rSlots.iSlot1 = -1;
		}
		if (rSlots.iSlot0 < 0 && rSlots.iSlot1 < 0)
		{
			edgeTriangles.erase(it);
		}
	}

	int32_t EdgeOther(uint64_t iKey, int32_t iTriangle) const
	{
		auto it = edgeTriangles.find(iKey);
		if (it == edgeTriangles.end())
		{
			return -1;
		}
		const EdgeSlots& rSlots = it->second;
		if (rSlots.iSlot0 == iTriangle)
		{
			return rSlots.iSlot1;
		}
		if (rSlots.iSlot1 == iTriangle)
		{
			return rSlots.iSlot0;
		}
		return -1;
	}

	bool IsBandEligible(uint32_t iA, uint32_t iB, uint32_t iC) const
	{
		float fMinZ = std::min({VertexZ(iA), VertexZ(iB), VertexZ(iC)});
		float fMaxZ = std::max({VertexZ(iA), VertexZ(iB), VertexZ(iC)});
		return fMinZ <= fBandMaxZ && fMaxZ >= fBandMinZ;
	}

	float LongestEdgeXY(uint32_t iA, uint32_t iB, uint32_t iC) const
	{
		auto LengthSquared = [this](uint32_t iU, uint32_t iV) -> float
		{
			float fDx = VertexX(iV) - VertexX(iU);
			float fDy = VertexY(iV) - VertexY(iU);
			return fDx * fDx + fDy * fDy;
		};
		float fMaxSquared = std::max({LengthSquared(iA, iB), LengthSquared(iB, iC), LengthSquared(iC, iA)});
		return std::sqrt(fMaxSquared);
	}

	uint32_t AddVertex(float fX, float fY, float fZ)
	{
		uint32_t iNewIndex = static_cast<uint32_t>(meshPositions.size() / 3);
		meshPositions.push_back(fX);
		meshPositions.push_back(fY);
		meshPositions.push_back(fZ);
		return iNewIndex;
	}

	uint32_t GetOrCreateMidpoint(uint32_t iA, uint32_t iB)
	{
		uint64_t iKey = EdgeKey(iA, iB);
		auto it = edgeMidpoints.find(iKey);
		if (it != edgeMidpoints.end())
		{
			return it->second;
		}
		float fMidX = 0.5f * (VertexX(iA) + VertexX(iB));
		float fMidY = 0.5f * (VertexY(iA) + VertexY(iB));
		float fMidZ = 0.5f * (VertexZ(iA) + VertexZ(iB));
		uint32_t iMidpoint = AddVertex(fMidX, fMidY, fMidZ);
		edgeMidpoints.emplace(iKey, iMidpoint);
		return iMidpoint;
	}

	uint32_t LookupMidpoint(uint32_t iA, uint32_t iB) const
	{
		auto it = edgeMidpoints.find(EdgeKey(iA, iB));
		return it != edgeMidpoints.end() ? it->second : UINT32_MAX;
	}

	uint32_t AppendTriangle(uint32_t iA, uint32_t iB, uint32_t iC, uint8_t iDepth)
	{
		uint32_t iNewTriangle = static_cast<uint32_t>(meshIndices.size() / 3);
		meshIndices.push_back(iA);
		meshIndices.push_back(iB);
		meshIndices.push_back(iC);
		triangleAlive.push_back(1);
		triangleDepth.push_back(iDepth);
		EdgeAdd(EdgeKey(iA, iB), static_cast<int32_t>(iNewTriangle));
		EdgeAdd(EdgeKey(iB, iC), static_cast<int32_t>(iNewTriangle));
		EdgeAdd(EdgeKey(iC, iA), static_cast<int32_t>(iNewTriangle));
		return iNewTriangle;
	}

	void KillTriangle(uint32_t iTriangle)
	{
		uint32_t iA = meshIndices[static_cast<size_t>(iTriangle) * 3 + 0];
		uint32_t iB = meshIndices[static_cast<size_t>(iTriangle) * 3 + 1];
		uint32_t iC = meshIndices[static_cast<size_t>(iTriangle) * 3 + 2];
		EdgeRemove(EdgeKey(iA, iB), static_cast<int32_t>(iTriangle));
		EdgeRemove(EdgeKey(iB, iC), static_cast<int32_t>(iTriangle));
		EdgeRemove(EdgeKey(iC, iA), static_cast<int32_t>(iTriangle));
		triangleAlive[iTriangle] = 0;
		meshIndices[static_cast<size_t>(iTriangle) * 3 + 0] = UINT32_MAX;
		meshIndices[static_cast<size_t>(iTriangle) * 3 + 1] = UINT32_MAX;
		meshIndices[static_cast<size_t>(iTriangle) * 3 + 2] = UINT32_MAX;
	}

	std::vector<float>& meshPositions;
	std::vector<uint32_t>& meshIndices;
	float fBandMinZ;
	float fBandMaxZ;
	float fMaxEdge;
	int32_t iMaxDepth;
	int64_t& riDepthCapHits;

	std::unordered_map<uint64_t, uint32_t> edgeMidpoints;
	std::unordered_map<uint64_t, EdgeSlots> edgeTriangles;
	std::deque<uint32_t> worklist;
	std::vector<uint8_t> triangleAlive;
	std::vector<uint8_t> triangleDepth;
};

void BeachSubdivider::Run()
{
	int64_t iInitialTriangleCount = static_cast<int64_t>(meshIndices.size() / 3);
	triangleAlive.assign(static_cast<size_t>(iInitialTriangleCount), 1);
	triangleDepth.assign(static_cast<size_t>(iInitialTriangleCount), 0);

	// Build initial edge adjacency.
	for (int64_t iTriangle = 0; iTriangle < iInitialTriangleCount; ++iTriangle)
	{
		uint32_t iA = meshIndices[static_cast<size_t>(iTriangle) * 3 + 0];
		uint32_t iB = meshIndices[static_cast<size_t>(iTriangle) * 3 + 1];
		uint32_t iC = meshIndices[static_cast<size_t>(iTriangle) * 3 + 2];
		EdgeAdd(EdgeKey(iA, iB), static_cast<int32_t>(iTriangle));
		EdgeAdd(EdgeKey(iB, iC), static_cast<int32_t>(iTriangle));
		EdgeAdd(EdgeKey(iC, iA), static_cast<int32_t>(iTriangle));
	}

	// Seed worklist with every initial triangle that is in-band AND oversized.
	for (int64_t iTriangle = 0; iTriangle < iInitialTriangleCount; ++iTriangle)
	{
		uint32_t iA = meshIndices[static_cast<size_t>(iTriangle) * 3 + 0];
		uint32_t iB = meshIndices[static_cast<size_t>(iTriangle) * 3 + 1];
		uint32_t iC = meshIndices[static_cast<size_t>(iTriangle) * 3 + 2];
		if (IsBandEligible(iA, iB, iC) && LongestEdgeXY(iA, iB, iC) > fMaxEdge)
		{
			worklist.push_back(static_cast<uint32_t>(iTriangle));
		}
	}

	while (!worklist.empty())
	{
		uint32_t iTriangle = worklist.front();
		worklist.pop_front();
		if (triangleAlive[iTriangle] == 0)
		{
			continue;
		}
		if (triangleDepth[iTriangle] >= iMaxDepth)
		{
			++riDepthCapHits;
			continue;
		}

		uint32_t iA = meshIndices[static_cast<size_t>(iTriangle) * 3 + 0];
		uint32_t iB = meshIndices[static_cast<size_t>(iTriangle) * 3 + 1];
		uint32_t iC = meshIndices[static_cast<size_t>(iTriangle) * 3 + 2];

		bool bBandTrigger = IsBandEligible(iA, iB, iC) && LongestEdgeXY(iA, iB, iC) > fMaxEdge;
		uint32_t iMidpointAB = LookupMidpoint(iA, iB);
		uint32_t iMidpointBC = LookupMidpoint(iB, iC);
		uint32_t iMidpointCA = LookupMidpoint(iC, iA);
		int32_t iExistingMidpoints = (iMidpointAB != UINT32_MAX) + (iMidpointBC != UINT32_MAX) + (iMidpointCA != UINT32_MAX);

		if (!bBandTrigger && iExistingMidpoints == 0)
		{
			continue;
		}

		uint8_t iChildDepth = static_cast<uint8_t>(triangleDepth[iTriangle] + 1);

		if (bBandTrigger)
		{
			SplitInBand(iTriangle, iA, iB, iC, iMidpointAB, iMidpointBC, iMidpointCA, iChildDepth);
			continue;
		}

		AbsorbMidpoints(iTriangle, iA, iB, iC, iMidpointAB, iMidpointBC, iMidpointCA, iExistingMidpoints, iChildDepth);
	}

	// Compact: drop dead triangles (UINT32_MAX sentinel indices).
	std::vector<uint32_t> compactedIndices;
	compactedIndices.reserve(meshIndices.size());
	for (size_t i = 0; i < meshIndices.size(); i += 3)
	{
		if (meshIndices[i] == UINT32_MAX)
		{
			continue;
		}
		compactedIndices.push_back(meshIndices[i + 0]);
		compactedIndices.push_back(meshIndices[i + 1]);
		compactedIndices.push_back(meshIndices[i + 2]);
	}
	meshIndices = std::move(compactedIndices);
}

void BeachSubdivider::SplitInBand(uint32_t iTriangle, uint32_t iA, uint32_t iB, uint32_t iC, uint32_t iMidpointAB, uint32_t iMidpointBC, uint32_t iMidpointCA, uint8_t iChildDepth)
{
	// In-band 1->4 split. Capture neighbors BEFORE mutating edge tables so we know
	// who to notify on each parent edge. Create midpoints for any edges that don't
	// already have one; those new midpoints are what neighbors will absorb.
	int32_t iNeighborAB = EdgeOther(EdgeKey(iA, iB), static_cast<int32_t>(iTriangle));
	int32_t iNeighborBC = EdgeOther(EdgeKey(iB, iC), static_cast<int32_t>(iTriangle));
	int32_t iNeighborCA = EdgeOther(EdgeKey(iC, iA), static_cast<int32_t>(iTriangle));

	if (iMidpointAB == UINT32_MAX)
	{
		iMidpointAB = GetOrCreateMidpoint(iA, iB);
	}
	if (iMidpointBC == UINT32_MAX)
	{
		iMidpointBC = GetOrCreateMidpoint(iB, iC);
	}
	if (iMidpointCA == UINT32_MAX)
	{
		iMidpointCA = GetOrCreateMidpoint(iC, iA);
	}

	KillTriangle(iTriangle);

	uint32_t iChild0 = AppendTriangle(iA, iMidpointAB, iMidpointCA, iChildDepth);
	uint32_t iChild1 = AppendTriangle(iMidpointAB, iB, iMidpointBC, iChildDepth);
	uint32_t iChild2 = AppendTriangle(iMidpointCA, iMidpointBC, iC, iChildDepth);
	uint32_t iChild3 = AppendTriangle(iMidpointAB, iMidpointBC, iMidpointCA, iChildDepth);

	worklist.push_back(iChild0);
	worklist.push_back(iChild1);
	worklist.push_back(iChild2);
	worklist.push_back(iChild3);

	if (iNeighborAB >= 0 && triangleAlive[static_cast<size_t>(iNeighborAB)] != 0)
	{
		worklist.push_back(static_cast<uint32_t>(iNeighborAB));
	}
	if (iNeighborBC >= 0 && triangleAlive[static_cast<size_t>(iNeighborBC)] != 0)
	{
		worklist.push_back(static_cast<uint32_t>(iNeighborBC));
	}
	if (iNeighborCA >= 0 && triangleAlive[static_cast<size_t>(iNeighborCA)] != 0)
	{
		worklist.push_back(static_cast<uint32_t>(iNeighborCA));
	}
}

void BeachSubdivider::AbsorbMidpoints(uint32_t iTriangle, uint32_t iA, uint32_t iB, uint32_t iC, uint32_t iMidpointAB, uint32_t iMidpointBC, uint32_t iMidpointCA, int32_t iExistingMidpoints, uint8_t iChildDepth)
{
	// Absorption split: 1->2, 1->3, or 1->4 depending on midpoint count. No new midpoints
	// are created, so neighbors of this triangle gain no new T-junctions -- cascade firewall.
	KillTriangle(iTriangle);

	if (iExistingMidpoints == 1)
	{
		if (iMidpointAB != UINT32_MAX)
		{
			AppendTriangle(iA, iMidpointAB, iC, iChildDepth);
			AppendTriangle(iMidpointAB, iB, iC, iChildDepth);
		}
		else if (iMidpointBC != UINT32_MAX)
		{
			AppendTriangle(iA, iB, iMidpointBC, iChildDepth);
			AppendTriangle(iA, iMidpointBC, iC, iChildDepth);
		}
		else
		{
			AppendTriangle(iA, iB, iMidpointCA, iChildDepth);
			AppendTriangle(iB, iC, iMidpointCA, iChildDepth);
		}
	}
	else if (iExistingMidpoints == 2)
	{
		if (iMidpointAB != UINT32_MAX && iMidpointBC != UINT32_MAX)
		{
			// Midpoints on A-B and B-C. Corner B is between them.
			AppendTriangle(iMidpointAB, iB, iMidpointBC, iChildDepth);
			AppendTriangle(iA, iMidpointAB, iMidpointBC, iChildDepth);
			AppendTriangle(iA, iMidpointBC, iC, iChildDepth);
		}
		else if (iMidpointBC != UINT32_MAX && iMidpointCA != UINT32_MAX)
		{
			// Midpoints on B-C and C-A. Corner C is between them.
			AppendTriangle(iMidpointBC, iC, iMidpointCA, iChildDepth);
			AppendTriangle(iB, iMidpointBC, iMidpointCA, iChildDepth);
			AppendTriangle(iA, iB, iMidpointCA, iChildDepth);
		}
		else
		{
			// Midpoints on A-B and C-A. Corner A is between them.
			AppendTriangle(iA, iMidpointAB, iMidpointCA, iChildDepth);
			AppendTriangle(iMidpointAB, iB, iMidpointCA, iChildDepth);
			AppendTriangle(iMidpointCA, iB, iC, iChildDepth);
		}
	}
	else
	{
		// All three edges have midpoints. True 1->4 using the existing midpoints.
		AppendTriangle(iA, iMidpointAB, iMidpointCA, iChildDepth);
		AppendTriangle(iMidpointAB, iB, iMidpointBC, iChildDepth);
		AppendTriangle(iMidpointCA, iMidpointBC, iC, iChildDepth);
		AppendTriangle(iMidpointAB, iMidpointBC, iMidpointCA, iChildDepth);
	}
	// Absorption children inherit the parent's out-of-band Z-range (Z-range of children
	// is a subset of the parent's), so they cannot trigger a band split themselves --
	// no need to push them. They will be re-pushed automatically if a future in-band
	// split creates a new midpoint on one of their edges (via the iNeighbor* lookups).
}

} // namespace

void SubdivideBeachBand(std::vector<float>& rMeshPositions, std::vector<uint32_t>& rMeshIndices, const SubdivisionConfig& rConfig, int64_t& riDepthCapHits)
{
	BeachSubdivider subdivider(rMeshPositions, rMeshIndices, rConfig, riDepthCapHits);
	subdivider.Run();
}
