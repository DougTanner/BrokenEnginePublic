#include "NavBuild.h"

namespace engine
{

namespace
{

struct ContourEdge
{
	XMFLOAT2 f2A {};
	XMFLOAT2 f2B {};
};

// Convert a normalized heightmap sample to world-space elevation
float HeightmapToWorldElevation(float fNormalized, float fBeachElevation)
{
	float fRelative = fNormalized - fBeachElevation;
	if (fRelative >= 0.0f)
	{
		return gIslandHeight.Get() * fRelative;
	}
	return gWaterDepth.Get() * fRelative;
}

// Marching squares: extract isocontour edges at the given world-space elevation threshold
void ExtractContourEdges(std::vector<ContourEdge>& rEdges, const float* pfHeightmapData, int32_t iWidth, int32_t iHeight, float fBeachElevation, float fWorldThreshold)
{
	float fWidthScale = 1.0f / static_cast<float>(iWidth - 1);
	float fHeightScale = 1.0f / static_cast<float>(iHeight - 1);

	for (int32_t iY = 0; iY < iHeight - 1; ++iY)
	{
		for (int32_t iX = 0; iX < iWidth - 1; ++iX)
		{
			// Corner values converted to world-space elevation
			float fTL = HeightmapToWorldElevation(pfHeightmapData[iY * iWidth + iX], fBeachElevation);
			float fTR = HeightmapToWorldElevation(pfHeightmapData[iY * iWidth + iX + 1], fBeachElevation);
			float fBR = HeightmapToWorldElevation(pfHeightmapData[(iY + 1) * iWidth + iX + 1], fBeachElevation);
			float fBL = HeightmapToWorldElevation(pfHeightmapData[(iY + 1) * iWidth + iX], fBeachElevation);

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
			float fCellU = static_cast<float>(iX) * fWidthScale;
			float fCellV = static_cast<float>(iY) * fHeightScale;
			float fStepU = fWidthScale;
			float fStepV = fHeightScale;

			// Edge midpoints via linear interpolation
			auto Lerp = [](float fA, float fB, float fThresholdValue) -> float
			{
				float fDenom = fA - fB;
				if (std::abs(fDenom) < 1e-8f)
				{
					return 0.5f;
				}
				return std::clamp((fA - fThresholdValue) / fDenom, 0.0f, 1.0f);
			};

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
					rEdges.push_back({f2Bottom, f2Left});
					break;
				case 2:
					rEdges.push_back({f2Right, f2Bottom});
					break;
				case 3:
					rEdges.push_back({f2Right, f2Left});
					break;
				case 4:
					rEdges.push_back({f2Top, f2Right});
					break;
				case 5:
				{
					float fCenter = (fTL + fTR + fBR + fBL) * 0.25f;
					if (fCenter >= fWorldThreshold)
					{
						rEdges.push_back({f2Top, f2Left});
						rEdges.push_back({f2Bottom, f2Right});
					}
					else
					{
						rEdges.push_back({f2Top, f2Right});
						rEdges.push_back({f2Bottom, f2Left});
					}
					break;
				}
				case 6:
					rEdges.push_back({f2Top, f2Bottom});
					break;
				case 7:
					rEdges.push_back({f2Top, f2Left});
					break;
				case 8:
					rEdges.push_back({f2Left, f2Top});
					break;
				case 9:
					rEdges.push_back({f2Bottom, f2Top});
					break;
				case 10:
				{
					float fCenter = (fTL + fTR + fBR + fBL) * 0.25f;
					if (fCenter >= fWorldThreshold)
					{
						rEdges.push_back({f2Left, f2Bottom});
						rEdges.push_back({f2Right, f2Top});
					}
					else
					{
						rEdges.push_back({f2Left, f2Top});
						rEdges.push_back({f2Right, f2Bottom});
					}
					break;
				}
				case 11:
					rEdges.push_back({f2Right, f2Top});
					break;
				case 12:
					rEdges.push_back({f2Left, f2Right});
					break;
				case 13:
					rEdges.push_back({f2Bottom, f2Right});
					break;
				case 14:
					rEdges.push_back({f2Left, f2Bottom});
					break;
			}
		}
	}
}

// Chain contour edges into closed polygons by matching endpoints
void ChainEdgesIntoPolygons(std::vector<std::vector<XMFLOAT2>>& rPolygons, const std::vector<ContourEdge>& rEdges)
{
	static constexpr float kfEpsilonSq = 1e-10f;

	auto DistanceSq = [](const XMFLOAT2& rA, const XMFLOAT2& rB) -> float
	{
		float fDx = rA.x - rB.x;
		float fDy = rA.y - rB.y;
		return fDx * fDx + fDy * fDy;
	};

	// Quantize vertex positions to grid keys for O(1) lookup
	// Marching squares vertices land on half-pixel boundaries, so scale to integers
	static constexpr float kfQuantizeScale = 100000.0f;
	auto QuantizeKey = [](const XMFLOAT2& rPoint) -> uint64_t
	{
		auto iX = static_cast<int32_t>(rPoint.x * kfQuantizeScale + 0.5f);
		auto iY = static_cast<int32_t>(rPoint.y * kfQuantizeScale + 0.5f);
		return (static_cast<uint64_t>(static_cast<uint32_t>(iX)) << 32) | static_cast<uint64_t>(static_cast<uint32_t>(iY));
	};

	// Build adjacency: map from quantized vertex position to list of (edge index, which endpoint)
	struct EdgeRef
	{
		size_t iEdgeIndex;
		bool bIsEndpointB; // false = matched on f2A, true = matched on f2B
	};
	std::unordered_multimap<uint64_t, EdgeRef> vertexToEdge;
	vertexToEdge.reserve(rEdges.size() * 2);
	for (size_t i = 0; i < rEdges.size(); ++i)
	{
		vertexToEdge.insert({QuantizeKey(rEdges.at(i).f2A), {i, false}});
		vertexToEdge.insert({QuantizeKey(rEdges.at(i).f2B), {i, true}});
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

		bool bGrowing = true;
		while (bGrowing)
		{
			bGrowing = false;
			uint64_t uiTailKey = QuantizeKey(polygon.back());

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
				}
				else
				{
					polygon.push_back(rEdges.at(j).f2B);
				}
				used.at(j) = true;
				bGrowing = true;
				break;
			}
		}

		// Only keep closed polygons with enough vertices
		if (polygon.size() >= 3 && DistanceSq(polygon.front(), polygon.back()) < kfEpsilonSq)
		{
			polygon.pop_back();
			rPolygons.push_back(std::move(polygon));
		}
	}
}

// Ramer-Douglas-Peucker simplification
float PerpendicularDistanceSq(const XMFLOAT2& rPoint, const XMFLOAT2& rLineA, const XMFLOAT2& rLineB)
{
	float fDx = rLineB.x - rLineA.x;
	float fDy = rLineB.y - rLineA.y;
	float fLenSq = fDx * fDx + fDy * fDy;
	if (fLenSq < 1e-12f)
	{
		float fPx = rPoint.x - rLineA.x;
		float fPy = rPoint.y - rLineA.y;
		return fPx * fPx + fPy * fPy;
	}

	float fCross = std::abs((rPoint.x - rLineA.x) * fDy - (rPoint.y - rLineA.y) * fDx);
	return (fCross * fCross) / fLenSq;
}

void SimplifyRDP(std::vector<XMFLOAT2>& rResult, const std::vector<XMFLOAT2>& rPoints, size_t iStart, size_t iEnd, float fToleranceSq)
{
	float fMaxDistSq = 0.0f;
	size_t iMaxIndex = iStart;

	for (size_t i = iStart + 1; i < iEnd; ++i)
	{
		float fDistSq = PerpendicularDistanceSq(rPoints.at(i), rPoints.at(iStart), rPoints.at(iEnd));
		if (fDistSq > fMaxDistSq)
		{
			fMaxDistSq = fDistSq;
			iMaxIndex = i;
		}
	}

	if (fMaxDistSq > fToleranceSq)
	{
		SimplifyRDP(rResult, rPoints, iStart, iMaxIndex, fToleranceSq);
		SimplifyRDP(rResult, rPoints, iMaxIndex, iEnd, fToleranceSq);
	}
	else
	{
		rResult.push_back(rPoints.at(iEnd));
	}
}

void SimplifyPolygon(std::vector<XMFLOAT2>& rPolygon, float fTolerance)
{
	if (rPolygon.size() < 4)
	{
		return;
	}

	float fToleranceSq = fTolerance * fTolerance;

	// For closed polygons: simplify as a linear chain from first to last, then reconnect
	// Duplicate first point at the end to handle the closing segment
	std::vector<XMFLOAT2> open = rPolygon;
	open.push_back(rPolygon.front());

	std::vector<XMFLOAT2> simplified;
	simplified.push_back(open.front());
	SimplifyRDP(simplified, open, 0, open.size() - 1, fToleranceSq);

	// Remove the duplicated closing point
	if (simplified.size() > 1)
	{
		simplified.pop_back();
	}

	// Remove near-collinear vertices (angle ≈ 180°)
	std::vector<XMFLOAT2> cleaned;
	for (size_t i = 0; i < simplified.size(); ++i)
	{
		size_t iPrev = (i + simplified.size() - 1) % simplified.size();
		size_t iNext = (i + 1) % simplified.size();

		float fAxDiff = simplified.at(i).x - simplified.at(iPrev).x;
		float fAyDiff = simplified.at(i).y - simplified.at(iPrev).y;
		float fBxDiff = simplified.at(iNext).x - simplified.at(i).x;
		float fByDiff = simplified.at(iNext).y - simplified.at(i).y;

		float fCross = std::abs(fAxDiff * fByDiff - fAyDiff * fBxDiff);
		if (fCross > 1e-8f)
		{
			cleaned.push_back(simplified.at(i));
		}
	}

	if (cleaned.size() >= 3)
	{
		rPolygon = std::move(cleaned);
	}
}

// Compute signed area to determine winding order (positive = CCW)
float ComputeSignedArea(const std::vector<XMFLOAT2>& rPolygon)
{
	float fArea = 0.0f;
	for (size_t i = 0; i < rPolygon.size(); ++i)
	{
		size_t iNext = (i + 1) % rPolygon.size();
		fArea += rPolygon.at(i).x * rPolygon.at(iNext).y;
		fArea -= rPolygon.at(iNext).x * rPolygon.at(i).y;
	}
	return fArea * 0.5f;
}

// Inflate polygon outward by the given distance
// Uses vertex normal offset; skips inflation at acute concavities to avoid self-intersection
void InflatePolygon(std::vector<XMFLOAT2>& rPolygon, float fDistance)
{
	if (rPolygon.size() < 3)
	{
		return;
	}

	// Ensure CCW winding (positive signed area) so outward normals point away from obstacle
	float fSignedArea = ComputeSignedArea(rPolygon);
	if (fSignedArea < 0.0f)
	{
		std::reverse(rPolygon.begin(), rPolygon.end());
	}

	std::vector<XMFLOAT2> inflated;
	inflated.reserve(rPolygon.size());

	for (size_t i = 0; i < rPolygon.size(); ++i)
	{
		size_t iPrev = (i + rPolygon.size() - 1) % rPolygon.size();
		size_t iNext = (i + 1) % rPolygon.size();

		// Edge normals (outward for CCW polygon = rotate edge direction 90° clockwise)
		float fE1x = rPolygon.at(i).x - rPolygon.at(iPrev).x;
		float fE1y = rPolygon.at(i).y - rPolygon.at(iPrev).y;
		float fLen1 = std::sqrt(fE1x * fE1x + fE1y * fE1y);

		float fE2x = rPolygon.at(iNext).x - rPolygon.at(i).x;
		float fE2y = rPolygon.at(iNext).y - rPolygon.at(i).y;
		float fLen2 = std::sqrt(fE2x * fE2x + fE2y * fE2y);

		if (fLen1 < 1e-8f || fLen2 < 1e-8f)
		{
			inflated.push_back(rPolygon.at(i));
			continue;
		}

		// Outward normals for CCW polygon
		float fN1x = fE1y / fLen1;
		float fN1y = -fE1x / fLen1;
		float fN2x = fE2y / fLen2;
		float fN2y = -fE2x / fLen2;

		// Average normal
		float fNx = fN1x + fN2x;
		float fNy = fN1y + fN2y;
		float fNLen = std::sqrt(fNx * fNx + fNy * fNy);

		if (fNLen < 1e-8f)
		{
			// Edges are nearly parallel in opposite directions, skip inflation
			inflated.push_back(rPolygon.at(i));
			continue;
		}

		fNx /= fNLen;
		fNy /= fNLen;

		// Check interior angle: dot product of averaged normal with either edge normal
		// gives cos(half-angle). If the angle is too acute (concavity), limit inflation
		float fDot = fNx * fN1x + fNy * fN1y;
		float fScale = fDistance;
		if (fDot > 0.01f)
		{
			// Scale offset to maintain constant distance from edges (miter joint)
			fScale = fDistance / fDot;
			// Cap miter to avoid spikes at sharp angles
			static constexpr float kfMaxMiterScale = 3.0f;
			fScale = std::min(fScale, fDistance * kfMaxMiterScale);
		}

		inflated.push_back({rPolygon.at(i).x + fNx * fScale, rPolygon.at(i).y + fNy * fScale});
	}

	rPolygon = std::move(inflated);
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

void BuildNavContour(NavContour& rContour, const float* pfHeightmapData, int32_t iHeightmapWidth, int32_t iHeightmapHeight, float fBeachElevation, float fWorldThreshold)
{
	// Step 1: Extract contour edges via marching squares
	std::vector<ContourEdge> contourEdges;
	contourEdges.reserve(static_cast<size_t>(iHeightmapWidth) * static_cast<size_t>(iHeightmapHeight));
	ExtractContourEdges(contourEdges, pfHeightmapData, iHeightmapWidth, iHeightmapHeight, fBeachElevation, fWorldThreshold);

	if (contourEdges.empty())
	{
		return;
	}

	// Step 2: Chain edges into closed polygons
	std::vector<std::vector<XMFLOAT2>> polygons;
	ChainEdgesIntoPolygons(polygons, contourEdges);

	// Step 3: Simplify and inflate each polygon
	static constexpr float kfSimplifyTolerance = 0.01f;
	static constexpr float kfInflateDistance = 0.015f;

	for (std::vector<XMFLOAT2>& rPolygon : polygons)
	{
		SimplifyPolygon(rPolygon, kfSimplifyTolerance);
		InflatePolygon(rPolygon, kfInflateDistance);
	}

	// Step 4: Pack polygons into flat arrays
	for (const std::vector<XMFLOAT2>& rPolygon : polygons)
	{
		if (rPolygon.size() < 3)
		{
			continue;
		}

		rContour.polygonOffsets.push_back(static_cast<int32_t>(rContour.vertices.size()));
		for (const XMFLOAT2& rVertex : rPolygon)
		{
			rContour.vertices.push_back(rVertex);
		}
	}

	if (rContour.vertices.empty())
	{
		return;
	}

	// Step 5: Build visibility graph
	BuildVisibilityGraph(rContour);
}

void XM_CALLCONV BuildCellNavData(NavData& rNavData, const NavContour& rContour, FXMVECTOR vecArea, IslandsFlip eFlip, XMFLOAT2 f2IslandOffset, float fIslandWidth, float fIslandHeight)
{
	int32_t iVertexCount = static_cast<int32_t>(rContour.vertices.size());
	if (iVertexCount == 0)
	{
		return;
	}

	// Transform canonical UV vertices to world space
	float fIslandMinX = XMVectorGetX(vecArea) + f2IslandOffset.x;
	float fIslandMaxY = XMVectorGetY(vecArea) - f2IslandOffset.y;
	bool bFlipU = (eFlip & kFlipX) != 0;
	bool bFlipV = (eFlip & kFlipY) != 0;

	rNavData.vertices.resize(iVertexCount);
	for (int32_t i = 0; i < iVertexCount; ++i)
	{
		float fU = rContour.vertices.at(i).x;
		float fV = rContour.vertices.at(i).y;

		if (bFlipU)
		{
			fU = 1.0f - fU;
		}
		if (bFlipV)
		{
			fV = 1.0f - fV;
		}

		rNavData.vertices.at(i) = {fIslandMinX + fU * fIslandWidth, fIslandMaxY - fV * fIslandHeight};
	}

	// Copy topology (preserved across flips)
	rNavData.polygonOffsets = rContour.polygonOffsets;
	rNavData.visEdgeA = rContour.visEdgeA;
	rNavData.visEdgeB = rContour.visEdgeB;
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
}

} // namespace engine
