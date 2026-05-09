#include "NavBuild.h"

#include "Ui/TerrainWrappersBase.h"
#include "Ui/WaterWrappersBase.h"

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
		return gTerrainIslandHeight.Get() * fRelative;
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
		int32_t iX = static_cast<int32_t>(rPoint.x * kfQuantizeScale + 0.5f);
		int32_t iY = static_cast<int32_t>(rPoint.y * kfQuantizeScale + 0.5f);
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

// Detect dense vertex clusters and push them outward to smooth jagged boundaries
// Vertices with many non-adjacent neighbors within fClusterRadius get inflated by fExpansion along their outward normal
void SmoothDenseClusters(std::vector<XMFLOAT2>& rPolygon, float fClusterRadius, int32_t iMinClusterVertices, float fExpansion)
{
	if (rPolygon.size() < 4)
	{
		return;
	}

	float fRadiusSq = fClusterRadius * fClusterRadius;
	size_t iSize = rPolygon.size();

	// Ensure CCW winding so outward normals point away from obstacle
	float fSignedArea = ComputeSignedArea(rPolygon);
	if (fSignedArea < 0.0f)
	{
		std::reverse(rPolygon.begin(), rPolygon.end());
	}

	// Snapshot original positions so reads are not affected by writes
	std::vector<XMFLOAT2> original = rPolygon;

	// For each vertex, count non-adjacent vertices within the cluster radius
	for (size_t i = 0; i < iSize; ++i)
	{
		int32_t iNearbyCount = 0;
		for (size_t j = 0; j < iSize; ++j)
		{
			// Skip self and immediate polygon neighbors (trivially close)
			size_t iForward = (j + iSize - i) % iSize;
			size_t iBackward = (i + iSize - j) % iSize;
			size_t iSeparation = std::min(iForward, iBackward);
			if (iSeparation <= 2)
			{
				continue;
			}

			float fDx = original.at(j).x - original.at(i).x;
			float fDy = original.at(j).y - original.at(i).y;
			if (fDx * fDx + fDy * fDy < fRadiusSq)
			{
				++iNearbyCount;
			}
		}

		if (iNearbyCount < iMinClusterVertices)
		{
			continue;
		}

		// Push vertex outward along its vertex normal (same pattern as InflatePolygon)
		size_t iPrev = (i + iSize - 1) % iSize;
		size_t iNext = (i + 1) % iSize;

		float fE1x = original.at(i).x - original.at(iPrev).x;
		float fE1y = original.at(i).y - original.at(iPrev).y;
		float fLen1 = std::sqrt(fE1x * fE1x + fE1y * fE1y);

		float fE2x = original.at(iNext).x - original.at(i).x;
		float fE2y = original.at(iNext).y - original.at(i).y;
		float fLen2 = std::sqrt(fE2x * fE2x + fE2y * fE2y);

		if (fLen1 < 1e-8f || fLen2 < 1e-8f)
		{
			continue;
		}

		// Outward normals for CCW polygon (rotate edge direction 90 degrees clockwise)
		float fN1x = fE1y / fLen1;
		float fN1y = -fE1x / fLen1;
		float fN2x = fE2y / fLen2;
		float fN2y = -fE2x / fLen2;

		float fNx = fN1x + fN2x;
		float fNy = fN1y + fN2y;
		float fNLen = std::sqrt(fNx * fNx + fNy * fNy);
		if (fNLen < 1e-8f)
		{
			continue;
		}

		fNx /= fNLen;
		fNy /= fNLen;

		rPolygon.at(i).x = original.at(i).x + fNx * fExpansion;
		rPolygon.at(i).y = original.at(i).y + fNy * fExpansion;
	}
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

// Segment intersection returning the intersection point and parameters along each segment
struct IntersectionResult
{
	XMFLOAT2 f2Point {};
	float fTA {};
	float fTB {};
};

bool SegmentsIntersectAt(XMFLOAT2 f2A1, XMFLOAT2 f2A2, XMFLOAT2 f2B1, XMFLOAT2 f2B2, IntersectionResult& rResult)
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
	if (fT > kfSegmentEpsilon && fT < (1.0f - kfSegmentEpsilon) && fU > kfSegmentEpsilon && fU < (1.0f - kfSegmentEpsilon))
	{
		rResult.f2Point = {f2A1.x + fT * fD1x, f2A1.y + fT * fD1y};
		rResult.fTA = fT;
		rResult.fTB = fU;
		return true;
	}
	return false;
}

// Per-edge intersection record for the augmented vertex list
struct EdgeIntersection
{
	float fT {};
	XMFLOAT2 f2Point {};
	size_t iPartnerEdge {};  // Edge index on the other polygon
	size_t iSharedId {}; // Unique ID shared between matching intersection pairs in A and B
};

// Compute the union of two intersecting CCW polygons via boundary walk
std::vector<XMFLOAT2> ComputePolygonUnion(const std::vector<XMFLOAT2>& rPolyA, const std::vector<XMFLOAT2>& rPolyB)
{
	size_t iCountA = rPolyA.size();
	size_t iCountB = rPolyB.size();

	// Find all edge-edge intersection points
	// intersectionsA[edgeIndex] = sorted list of intersections along that edge of A
	std::vector<std::vector<EdgeIntersection>> intersectionsA(iCountA);
	std::vector<std::vector<EdgeIntersection>> intersectionsB(iCountB);

	for (size_t iA = 0; iA < iCountA; ++iA)
	{
		size_t iANext = (iA + 1) % iCountA;
		for (size_t iB = 0; iB < iCountB; ++iB)
		{
			size_t iBNext = (iB + 1) % iCountB;
			IntersectionResult result {};
			if (SegmentsIntersectAt(rPolyA.at(iA), rPolyA.at(iANext), rPolyB.at(iB), rPolyB.at(iBNext), result))
			{
				intersectionsA.at(iA).push_back({.fT = result.fTA, .f2Point = result.f2Point, .iPartnerEdge = iB, .iSharedId = 0,});
				intersectionsB.at(iB).push_back({.fT = result.fTB, .f2Point = result.f2Point, .iPartnerEdge = iA, .iSharedId = 0,});
			}
		}
	}

	// Sort intersections along each edge by parameter t
	for (std::vector<EdgeIntersection>& rList : intersectionsA)
	{
		std::sort(rList.begin(), rList.end(), [](const EdgeIntersection& rA, const EdgeIntersection& rB) { return rA.fT < rB.fT; });
	}
	for (std::vector<EdgeIntersection>& rList : intersectionsB)
	{
		std::sort(rList.begin(), rList.end(), [](const EdgeIntersection& rA, const EdgeIntersection& rB) { return rA.fT < rB.fT; });
	}

	// Build augmented vertex lists: original vertices interleaved with intersection points
	// Each entry is tagged: false = original vertex, true = intersection point
	struct AugmentedVertex
	{
		XMFLOAT2 f2Pos {};
		bool bIsIntersection {};
		size_t iIntersectionId {}; // Unique ID shared between the matching pair in A and B lists
	};

	// Assign unique IDs by matching intersection pairs
	// iSharedId == 0 means unassigned; valid IDs start at 1
	size_t iNextId = 1;
	for (size_t iA = 0; iA < iCountA; ++iA)
	{
		for (EdgeIntersection& rIntA : intersectionsA.at(iA))
		{
			if (rIntA.iSharedId != 0)
			{
				continue;
			}
			size_t iB = rIntA.iPartnerEdge;
			for (EdgeIntersection& rIntB : intersectionsB.at(iB))
			{
				if (rIntB.iSharedId != 0)
				{
					continue;
				}
				if (rIntB.iPartnerEdge == iA)
				{
					float fDx = rIntA.f2Point.x - rIntB.f2Point.x;
					float fDy = rIntA.f2Point.y - rIntB.f2Point.y;
					if (fDx * fDx + fDy * fDy < 1e-10f)
					{
						size_t iId = iNextId++;
						rIntA.iSharedId = iId;
						rIntB.iSharedId = iId;
						break;
					}
				}
			}
		}
	}

	// Count total entries for reserve
	size_t iAugCountA = iCountA;
	size_t iAugCountB = iCountB;
	for (const std::vector<EdgeIntersection>& rList : intersectionsA)
	{
		iAugCountA += rList.size();
	}
	for (const std::vector<EdgeIntersection>& rList : intersectionsB)
	{
		iAugCountB += rList.size();
	}

	std::vector<AugmentedVertex> augmentedA;
	std::vector<AugmentedVertex> augmentedB;
	augmentedA.reserve(iAugCountA);
	augmentedB.reserve(iAugCountB);

	// Build augmented list for A
	for (size_t i = 0; i < iCountA; ++i)
	{
		augmentedA.push_back({.f2Pos = rPolyA.at(i), .bIsIntersection = false, .iIntersectionId = 0,});
		for (const EdgeIntersection& rInt : intersectionsA.at(i))
		{
			augmentedA.push_back({.f2Pos = rInt.f2Point, .bIsIntersection = true, .iIntersectionId = rInt.iSharedId,});
		}
	}

	// Build augmented list for B
	for (size_t i = 0; i < iCountB; ++i)
	{
		augmentedB.push_back({.f2Pos = rPolyB.at(i), .bIsIntersection = false, .iIntersectionId = 0,});
		for (const EdgeIntersection& rInt : intersectionsB.at(i))
		{
			augmentedB.push_back({.f2Pos = rInt.f2Point, .bIsIntersection = true, .iIntersectionId = rInt.iSharedId,});
		}
	}

	// Build lookup: intersection ID -> index in augmentedA / augmentedB
	std::unordered_map<size_t, size_t> idToIndexA;
	std::unordered_map<size_t, size_t> idToIndexB;
	for (size_t i = 0; i < augmentedA.size(); ++i)
	{
		if (augmentedA.at(i).bIsIntersection)
		{
			idToIndexA.insert_or_assign(augmentedA.at(i).iIntersectionId, i);
		}
	}
	for (size_t i = 0; i < augmentedB.size(); ++i)
	{
		if (augmentedB.at(i).bIsIntersection)
		{
			idToIndexB.insert_or_assign(augmentedB.at(i).iIntersectionId, i);
		}
	}

	// Find starting vertex: leftmost vertex across both polygons (guaranteed on outer boundary)
	size_t iStartIndex = 0;
	bool bStartOnA = true;
	float fMinX = augmentedA.at(0).f2Pos.x;

	for (size_t i = 1; i < augmentedA.size(); ++i)
	{
		if (augmentedA.at(i).f2Pos.x < fMinX || (augmentedA.at(i).f2Pos.x == fMinX && augmentedA.at(i).f2Pos.y < augmentedA.at(iStartIndex).f2Pos.y))
		{
			fMinX = augmentedA.at(i).f2Pos.x;
			iStartIndex = i;
			bStartOnA = true;
		}
	}
	for (size_t i = 0; i < augmentedB.size(); ++i)
	{
		if (augmentedB.at(i).f2Pos.x < fMinX || (augmentedB.at(i).f2Pos.x == fMinX && augmentedB.at(i).f2Pos.y < (bStartOnA ? augmentedA.at(iStartIndex).f2Pos.y : augmentedB.at(iStartIndex).f2Pos.y)))
		{
			fMinX = augmentedB.at(i).f2Pos.x;
			iStartIndex = i;
			bStartOnA = false;
		}
	}

	// Boundary walk
	std::vector<XMFLOAT2> result;
	result.reserve(augmentedA.size() + augmentedB.size());
	bool bOnA = bStartOnA;
	size_t iCurrent = iStartIndex;
	size_t iMaxSteps = augmentedA.size() + augmentedB.size() + 2;

	// Test a point slightly past the intersection along the edge to avoid boundary-undefined PointInPolygon results
	static constexpr float kfProbeOffset = 0.001f;
	auto ProbePoint = [](const XMFLOAT2& rFrom, const XMFLOAT2& rTo) -> XMFLOAT2
	{
		return {rFrom.x + kfProbeOffset * (rTo.x - rFrom.x), rFrom.y + kfProbeOffset * (rTo.y - rFrom.y)};
	};

	for (size_t iStep = 0; iStep < iMaxSteps; ++iStep)
	{
		const std::vector<AugmentedVertex>& rAugmented = bOnA ? augmentedA : augmentedB;
		result.push_back(rAugmented.at(iCurrent).f2Pos);

		// Advance to next vertex in current polygon
		size_t iNext = (iCurrent + 1) % rAugmented.size();

		// If current vertex is an intersection, decide whether to switch polygons
		if (rAugmented.at(iCurrent).bIsIntersection)
		{
			size_t iId = rAugmented.at(iCurrent).iIntersectionId;
			XMFLOAT2 f2Current = rAugmented.at(iCurrent).f2Pos;
			if (bOnA)
			{
				std::unordered_map<size_t, size_t>::iterator it = idToIndexB.find(iId);
				if (it != idToIndexB.end())
				{
					// At a union boundary, follow the path that stays outside the other polygon
					size_t iNextOnA = (iCurrent + 1) % augmentedA.size();
					XMFLOAT2 f2Probe = ProbePoint(f2Current, augmentedA.at(iNextOnA).f2Pos);
					bool bNextAInsideB = PointInPolygon(f2Probe, rPolyB.data(), static_cast<int32_t>(iCountB));
					if (bNextAInsideB)
					{
						bOnA = false;
						iCurrent = (it->second + 1) % augmentedB.size();
						if (iCurrent == iStartIndex && bOnA == bStartOnA)
						{
							break;
						}
						continue;
					}
				}
			}
			else
			{
				std::unordered_map<size_t, size_t>::iterator it = idToIndexA.find(iId);
				if (it != idToIndexA.end())
				{
					size_t iNextOnB = (iCurrent + 1) % augmentedB.size();
					XMFLOAT2 f2Probe = ProbePoint(f2Current, augmentedB.at(iNextOnB).f2Pos);
					bool bNextBInsideA = PointInPolygon(f2Probe, rPolyA.data(), static_cast<int32_t>(iCountA));
					if (bNextBInsideA)
					{
						bOnA = true;
						iCurrent = (it->second + 1) % augmentedA.size();
						if (iCurrent == iStartIndex && bOnA == bStartOnA)
						{
							break;
						}
						continue;
					}
				}
			}
		}

		iCurrent = iNext;
		if (iCurrent == iStartIndex && bOnA == bStartOnA)
		{
			break;
		}
	}

	if (result.size() >= iMaxSteps)
	{
		LOG(kNavData, kWarning, "NavBuild: boundary walk exhausted step limit ({} steps), result may be malformed", iMaxSteps);
	}

	// Ensure CCW winding
	if (ComputeSignedArea(result) < 0.0f)
	{
		std::reverse(result.begin(), result.end());
	}

	return result;
}

// Merge overlapping polygons: combine intersecting pairs and remove contained polygons
void MergeOverlappingPolygons(std::vector<std::vector<XMFLOAT2>>& rPolygons)
{
	bool bMerged = true;
	while (bMerged)
	{
		bMerged = false;
		for (size_t i = 0; i < rPolygons.size() && !bMerged; ++i)
		{
			for (size_t j = i + 1; j < rPolygons.size() && !bMerged; ++j)
			{
				const std::vector<XMFLOAT2>& rPolyA = rPolygons.at(i);
				const std::vector<XMFLOAT2>& rPolyB = rPolygons.at(j);

				// Check for edge-edge intersections
				bool bIntersects = false;
				for (size_t iA = 0; iA < rPolyA.size() && !bIntersects; ++iA)
				{
					size_t iANext = (iA + 1) % rPolyA.size();
					for (size_t iB = 0; iB < rPolyB.size() && !bIntersects; ++iB)
					{
						size_t iBNext = (iB + 1) % rPolyB.size();
						if (SegmentsIntersect(rPolyA.at(iA), rPolyA.at(iANext), rPolyB.at(iB), rPolyB.at(iBNext)))
						{
							bIntersects = true;
						}
					}
				}

				if (bIntersects)
				{
					std::vector<XMFLOAT2> merged = ComputePolygonUnion(rPolyA, rPolyB);
					if (merged.size() >= 3)
					{
						LOG(kNavData, kDebug, "NavBuild: merged polygons {} ({} verts) and {} ({} verts) -> {} verts", i, rPolyA.size(), j, rPolyB.size(), merged.size());
						rPolygons.at(i) = std::move(merged);
						rPolygons.erase(rPolygons.begin() + static_cast<int64_t>(j));
						bMerged = true;
					}
				}
				else
				{
					bool bAInsideB = PointInPolygon(rPolyA.at(0), rPolyB.data(), static_cast<int32_t>(rPolyB.size()));
					if (bAInsideB)
					{
						LOG(kNavData, kDebug, "NavBuild: polygon {} ({} verts) contained in polygon {} ({} verts), removing inner", i, rPolyA.size(), j, rPolyB.size());
						rPolygons.erase(rPolygons.begin() + static_cast<int64_t>(i));
						bMerged = true;
					}
					else
					{
						bool bBInsideA = PointInPolygon(rPolyB.at(0), rPolyA.data(), static_cast<int32_t>(rPolyA.size()));
						if (bBInsideA)
						{
							LOG(kNavData, kDebug, "NavBuild: polygon {} ({} verts) contained in polygon {} ({} verts), removing inner", j, rPolyB.size(), i, rPolyA.size());
							rPolygons.erase(rPolygons.begin() + static_cast<int64_t>(j));
							bMerged = true;
						}
					}
				}
			}
		}
	}
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
	LOG(kNavData, kDebug, "NavBuild: heightmap {}x{} beachElev={} worldThreshold={}", iHeightmapWidth, iHeightmapHeight, fBeachElevation, fWorldThreshold);

	// Step 1: Extract contour edges via marching squares
	std::vector<ContourEdge> contourEdges;
	contourEdges.reserve(static_cast<size_t>(iHeightmapWidth) * static_cast<size_t>(iHeightmapHeight));
	ExtractContourEdges(contourEdges, pfHeightmapData, iHeightmapWidth, iHeightmapHeight, fBeachElevation, fWorldThreshold);

	LOG(kNavData, kDebug, "NavBuild: extracted {} contour edges", contourEdges.size());

	if (contourEdges.empty())
	{
		return;
	}

	// Step 2: Chain edges into closed polygons
	std::vector<std::vector<XMFLOAT2>> polygons;
	ChainEdgesIntoPolygons(polygons, contourEdges);

	LOG(kNavData, kDebug, "NavBuild: chained into {} polygons", polygons.size());

	// Step 3: Simplify, smooth dense clusters, and inflate each polygon
	static constexpr float kfSimplifyTolerance = 0.01f;
	static constexpr float kfClusterRadius = 0.05f;
	static constexpr int32_t kiMinClusterVertices = 5;
	static constexpr float kfClusterExpansion = 0.02f;
	static constexpr float kfInflateDistance = 0.015f;

	for (size_t iPoly = 0; iPoly < polygons.size(); ++iPoly)
	{
		int64_t iPreSimplify = static_cast<int64_t>(polygons.at(iPoly).size());
		SimplifyPolygon(polygons.at(iPoly), kfSimplifyTolerance);
		SmoothDenseClusters(polygons.at(iPoly), kfClusterRadius, kiMinClusterVertices, kfClusterExpansion);
		SimplifyPolygon(polygons.at(iPoly), kfSimplifyTolerance);
		int64_t iPostSimplify = static_cast<int64_t>(polygons.at(iPoly).size());
		InflatePolygon(polygons.at(iPoly), kfInflateDistance);
		LOG(kNavData, kDebug, "NavBuild: polygon {} verts: {} -> {} (after simplify+smooth)", iPoly, iPreSimplify, iPostSimplify);
	}

	// Step 4: Merge overlapping polygons (inflation can cause nearby polygons to intersect)
	size_t iPreMergeCount = polygons.size();
	MergeOverlappingPolygons(polygons);
	if (polygons.size() != iPreMergeCount)
	{
		LOG(kNavData, kDebug, "NavBuild: merged {} polygons -> {}", iPreMergeCount, polygons.size());
	}

	// Step 5: Pack polygons into flat arrays
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

	LOG(kNavData, kDebug, "NavBuild: total vertices={} polygons={}", rContour.vertices.size(), rContour.polygonOffsets.size());

	if (rContour.vertices.empty())
	{
		return;
	}

	// Step 6: Build visibility graph
	BuildVisibilityGraph(rContour);

	LOG(kNavData, kDebug, "NavBuild: visibility graph edges={}", rContour.visEdgeA.size());
}

void XM_CALLCONV BuildCellNavData(NavData& rNavData, const NavContour& rContour, FXMVECTOR vecArea, float fAngle, XMFLOAT2 f2IslandOffset, float fIslandWidth, float fIslandHeight)
{
	int32_t iVertexCount = static_cast<int32_t>(rContour.vertices.size());
	if (iVertexCount == 0)
	{
		return;
	}

	// Rotate canonical UV vertices around island center, then place in world space.
	// Local axes: +U = +world.x, +V = -world.y (V is world-Y inverted).
	float fIslandMinX = XMVectorGetX(vecArea) + f2IslandOffset.x;
	float fIslandMaxY = XMVectorGetY(vecArea) - f2IslandOffset.y;
	float fCenterX = fIslandMinX + 0.5f * fIslandWidth;
	float fCenterY = fIslandMaxY - 0.5f * fIslandHeight;
	float fCos = std::cos(fAngle);
	float fSin = std::sin(fAngle);

	rNavData.vertices.resize(iVertexCount);
	for (int32_t i = 0; i < iVertexCount; ++i)
	{
		float fU = rContour.vertices.at(i).x;
		float fV = rContour.vertices.at(i).y;

		float fLocalX = (fU - 0.5f) * fIslandWidth;
		float fLocalY = (0.5f - fV) * fIslandHeight;

		float fRotX = fLocalX * fCos - fLocalY * fSin;
		float fRotY = fLocalX * fSin + fLocalY * fCos;

		rNavData.vertices.at(i) = {fCenterX + fRotX, fCenterY + fRotY};
	}

	// Copy topology (preserved across rotation)
	rNavData.polygonOffsets = rContour.polygonOffsets;
	rNavData.visEdgeA = rContour.visEdgeA;
	rNavData.visEdgeB = rContour.visEdgeB;

	// Log per-polygon vertex positions for density analysis
	for (size_t iPoly = 0; iPoly < rNavData.polygonOffsets.size(); ++iPoly)
	{
		int32_t iStart = rNavData.polygonOffsets.at(iPoly);
		int32_t iEnd = (iPoly + 1 < rNavData.polygonOffsets.size()) ? rNavData.polygonOffsets.at(iPoly + 1) : iVertexCount;
		int32_t iCount = iEnd - iStart;

		// Compute bounding box to detect tight clusters
		float fMinX = std::numeric_limits<float>::max();
		float fMaxX = std::numeric_limits<float>::lowest();
		float fMinY = std::numeric_limits<float>::max();
		float fMaxY = std::numeric_limits<float>::lowest();
		for (int32_t i = iStart; i < iEnd; ++i)
		{
			fMinX = std::min(fMinX, rNavData.vertices.at(i).x);
			fMaxX = std::max(fMaxX, rNavData.vertices.at(i).x);
			fMinY = std::min(fMinY, rNavData.vertices.at(i).y);
			fMaxY = std::max(fMaxY, rNavData.vertices.at(i).y);
		}
		float fBoundsWidth = fMaxX - fMinX;
		float fBoundsHeight = fMaxY - fMinY;

		LOG(kNavData, kVerbose, "NavCell: polygon {} verts={} bounds=({} {})..({} {}) size={}x{}", iPoly, iCount, fMinX, fMinY, fMaxX, fMaxY, fBoundsWidth, fBoundsHeight);
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
}

} // namespace engine
