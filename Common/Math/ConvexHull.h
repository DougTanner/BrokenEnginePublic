#pragma once

namespace common
{

// A 2D convex hull in world space: CCW vertices plus a precomputed AABB for broadphase rejection.
// pVertices points into caller-owned storage (e.g. a scratch vector) that must outlive the hull;
// BuildWorldHull fills it from a template's island-local polygon. Used by IslandChainPlacement to
// pack islands by their true valid-area hull (rectangles may overlap underwater, hulls may not).
struct ConvexHull2D
{
	const XMFLOAT2* pVertices = nullptr;
	int32_t iVertexCount = 0;
	XMFLOAT2 f2AabbMin {};
	XMFLOAT2 f2AabbMax {};
};

// Rotate a CCW island-local hull (centered at origin) by fRotation (CCW, matching GlobalElevation's
// convention) and translate to f2WorldPos, writing the world-space verts into rOutVertices (caller
// scratch, >= iLocalCount entries). Returns a ConvexHull2D over rOutVertices with its AABB filled.
inline ConvexHull2D BuildWorldHull(const XMFLOAT2* pLocalVertices, int32_t iLocalCount, XMFLOAT2 f2WorldPos, float fRotation, XMFLOAT2* rOutVertices)
{
	float fCos = std::cos(fRotation);
	float fSin = std::sin(fRotation);

	XMFLOAT2 f2Min {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
	XMFLOAT2 f2Max {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
	for (int32_t i = 0; i < iLocalCount; ++i)
	{
		float fLocalX = pLocalVertices[i].x;
		float fLocalY = pLocalVertices[i].y;
		float fWorldX = f2WorldPos.x + fLocalX * fCos - fLocalY * fSin;
		float fWorldY = f2WorldPos.y + fLocalX * fSin + fLocalY * fCos;
		rOutVertices[i] = {fWorldX, fWorldY};
		f2Min.x = std::min(f2Min.x, fWorldX);
		f2Min.y = std::min(f2Min.y, fWorldY);
		f2Max.x = std::max(f2Max.x, fWorldX);
		f2Max.y = std::max(f2Max.y, fWorldY);
	}

	return {rOutVertices, iLocalCount, f2Min, f2Max};
}

// Broadphase: do the two hulls' precomputed AABBs overlap? Strict < so edge-touching AABBs count as
// non-overlapping (lets islands pack flush).
inline bool AabbsOverlap2D(const ConvexHull2D& rA, const ConvexHull2D& rB)
{
	return rA.f2AabbMin.x < rB.f2AabbMax.x && rB.f2AabbMin.x < rA.f2AabbMax.x
	    && rA.f2AabbMin.y < rB.f2AabbMax.y && rB.f2AabbMin.y < rA.f2AabbMax.y;
}

// Separating Axis Theorem for two CCW convex polygons. True iff they share interior area; edge-
// touching returns false so placement can pack hulls flush without registering overlap. Scale-
// invariant (axes are un-normalized edge normals). Deterministic: fixed axis order (A's edges then
// B's, ascending index) and pure scalar float math.
inline bool ConvexHullsOverlap(const ConvexHull2D& rA, const ConvexHull2D& rB)
{
	if (!AabbsOverlap2D(rA, rB))
	{
		return false;
	}

	for (int32_t iPoly = 0; iPoly < 2; ++iPoly)
	{
		const ConvexHull2D& rEdgeHull = (iPoly == 0) ? rA : rB;
		for (int32_t i = 0; i < rEdgeHull.iVertexCount; ++i)
		{
			const XMFLOAT2& rV0 = rEdgeHull.pVertices[i];
			const XMFLOAT2& rV1 = rEdgeHull.pVertices[(i + 1) % rEdgeHull.iVertexCount];
			// Outward normal of the CCW edge (v1 - v0) is (edge.y, -edge.x).
			float fAxisX = rV1.y - rV0.y;
			float fAxisY = rV0.x - rV1.x;

			float fMinA = std::numeric_limits<float>::max();
			float fMaxA = std::numeric_limits<float>::lowest();
			for (int32_t j = 0; j < rA.iVertexCount; ++j)
			{
				float fProj = rA.pVertices[j].x * fAxisX + rA.pVertices[j].y * fAxisY;
				fMinA = std::min(fMinA, fProj);
				fMaxA = std::max(fMaxA, fProj);
			}

			float fMinB = std::numeric_limits<float>::max();
			float fMaxB = std::numeric_limits<float>::lowest();
			for (int32_t j = 0; j < rB.iVertexCount; ++j)
			{
				float fProj = rB.pVertices[j].x * fAxisX + rB.pVertices[j].y * fAxisY;
				fMinB = std::min(fMinB, fProj);
				fMaxB = std::max(fMaxB, fProj);
			}

			if (fMaxA <= fMinB || fMaxB <= fMinA)
			{
				return false;
			}
		}
	}

	return true;
}

} // namespace common
