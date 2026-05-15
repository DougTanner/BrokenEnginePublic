#include "IslandPlacement.h"

#include "Frame/IslandTerrain.h"
#include "Frame/Frame.h"

#include "Data/Data.h"

namespace engine
{

namespace
{

inline constexpr uint64_t kCountSeedMultiplier = 0x52DCE72972D5C1CBull;
inline constexpr uint64_t kCrcPickSeedMultiplier = 0xD1B54A32D192ED03ull;
inline constexpr uint64_t kPositionSeedMultiplier = 0xCBF29CE484222325ull;

inline constexpr int32_t kMinIslandsPerCell = 1;
inline constexpr int32_t kMaxIslandsPerCell = 4;
inline constexpr int32_t kMaxRejectAttempts = 24;

struct RotatedAabb
{
	float fCenterX = 0.0f;
	float fCenterY = 0.0f;
	float fHalfW = 0.0f; // half-width of rotation-expanded AABB (axis-aligned bound of the rotated quad)
	float fHalfH = 0.0f;
};

RotatedAabb ComputeRotatedAabb(float fCenterX, float fCenterY, float fQuadW, float fQuadH, float fAngle)
{
	float fAbsCos = std::abs(std::cos(fAngle));
	float fAbsSin = std::abs(std::sin(fAngle));
	float fHalfW = 0.5f * (fQuadW * fAbsCos + fQuadH * fAbsSin);
	float fHalfH = 0.5f * (fQuadW * fAbsSin + fQuadH * fAbsCos);
	return {fCenterX, fCenterY, fHalfW, fHalfH};
}

bool AabbsOverlap(const RotatedAabb& rA, const RotatedAabb& rB)
{
	return std::abs(rA.fCenterX - rB.fCenterX) < (rA.fHalfW + rB.fHalfW)
	    && std::abs(rA.fCenterY - rB.fCenterY) < (rA.fHalfH + rB.fHalfH);
}

} // anonymous namespace

void GenerateIslandPlacements(GridCoord coord, std::vector<IslandPlacement>& rOut)
{
	rOut.clear();

	float fCellOriginX = game::Frame::kfBaseAreaMinX + static_cast<float>(coord.x) * game::Frame::kfCellWidth;
	float fCellOriginMaxY = game::Frame::kfBaseAreaMaxY + static_cast<float>(coord.y) * game::Frame::kfCellHeight;
	float fCellMaxX = fCellOriginX + game::Frame::kfCellWidth;
	float fCellMinY = fCellOriginMaxY - game::Frame::kfCellHeight;

	common::RandomEngine countRandom(game::SeedFromGridCoord(coord, kCountSeedMultiplier));
	int32_t iCount = kMinIslandsPerCell + static_cast<int32_t>(common::Random(static_cast<uint32_t>(kMaxIslandsPerCell - kMinIslandsPerCell), countRandom));

	common::RandomEngine crcRandom(game::SeedFromGridCoord(coord, kCrcPickSeedMultiplier));
	common::RandomEngine positionRandom(game::SeedFromGridCoord(coord, kPositionSeedMultiplier));

	std::vector<RotatedAabb> placedAabbs;
	placedAabbs.reserve(static_cast<size_t>(iCount));

	for (int32_t i = 0; i < iCount; ++i)
	{
		// common::Random(N, ...) is inclusive on N — pass size-1 as the max index.
		common::crc_t islandCrc = gpIslandTerrain->mIslandCrcsSorted.at(common::Random(static_cast<uint32_t>(gpIslandTerrain->mIslandCrcsSorted.size() - 1u), crcRandom));
		const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(islandCrc);
		float fQuadW = rTemplate.mfQuadFootprintX;
		float fQuadH = rTemplate.mfQuadFootprintY;

		bool bAccepted = false;
		for (int32_t iAttempt = 0; iAttempt < kMaxRejectAttempts && !bAccepted; ++iAttempt)
		{
			float fRotation = common::Random(2.0f * DirectX::XM_PI, positionRandom);

			float fAbsCos = std::abs(std::cos(fRotation));
			float fAbsSin = std::abs(std::sin(fRotation));
			float fRotHalfW = 0.5f * (fQuadW * fAbsCos + fQuadH * fAbsSin);
			float fRotHalfH = 0.5f * (fQuadW * fAbsSin + fQuadH * fAbsCos);

			// Center must lie within [cellMin + rotHalf, cellMax - rotHalf] so the rotated AABB fits.
			float fMinX = fCellOriginX + fRotHalfW;
			float fMaxX = fCellMaxX - fRotHalfW;
			float fMinY = fCellMinY + fRotHalfH;
			float fMaxY = fCellOriginMaxY - fRotHalfH;
			if (fMinX >= fMaxX || fMinY >= fMaxY)
			{
				// Island too large for cell at this rotation — skip and try another rotation.
				continue;
			}

			float fCenterX = fMinX + common::Random(fMaxX - fMinX, positionRandom);
			float fCenterY = fMinY + common::Random(fMaxY - fMinY, positionRandom);
			RotatedAabb candidate {fCenterX, fCenterY, fRotHalfW, fRotHalfH};

			bool bOverlap = false;
			for (const RotatedAabb& rPlaced : placedAabbs)
			{
				if (AabbsOverlap(candidate, rPlaced))
				{
					bOverlap = true;
					break;
				}
			}
			if (bOverlap)
			{
				continue;
			}

			rOut.push_back({.islandCrc = islandCrc, .f2WorldPos = {fCenterX, fCenterY}, .fRotation = fRotation});
			placedAabbs.push_back(candidate);
			bAccepted = true;
		}
		// On cap-hit, accept the fewer-island result rather than infinite-loop. The first placement
		// is mathematically guaranteed to fit (empty placedAabbs + rotated bbox < cell width), so
		// the assert below catches scale-knock-on bugs where the geometry contract is broken.
	}

	ASSERT(!rOut.empty());
}

} // namespace engine
