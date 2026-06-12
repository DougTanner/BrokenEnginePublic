#include "BakeIslandIntermediatesInternal.h"

// Crops one chunk region [xStart,xEnd) x [yStart,yEnd) out of the full Gaea bake and writes the
// chunk's per-region geometry (Elevation.r32, AmbientOcclusion.r16, MeshProcessed.bin,
// BakedDimensions.json) into the leaf's own Intermediates/ folder (rLeafDir/Intermediates) — kept
// under "Intermediates" so the same .gitignore rule that ignores the raw route bake ignores these
// derived per-chunk files too; the committed BC outputs land at the leaf root later (ExportIsland).
// The shared full-res Color/Normals/mask PNGs are NOT copied — rTextureSourceDirRelative
// (leaf-relative, "../Intermediates") points ExportIsland at the route's Intermediates folder, and
// the crop rect recorded in BakedDimensions lets it crop them in-memory. A 1x1 route passes the
// whole texture as the region; a 2x1 route calls this once per Y-half, a 2x2 route once per quadrant
// (both axes split at the midpoints), so the existing auto-crop runs (and, in ExportIsland, the
// underwater re-coloring) once per chunk.
// meshPositions / meshIndices are taken by value so each region mutates its own copy of the shared
// post-subdivision mesh.
// Returns true if the leaf was written, false if rejected (too low — see kfMinIslandMaxHeightMeters).
bool ProcessBakedRegion(const IslandBakeContext& rContext, const BakeOutput& rBakeOutput, const RegionBounds& rRegion, std::vector<float> meshPositions, std::vector<uint32_t> meshIndices, const LeafTarget& rLeaf)
{
	const int64_t iTexturePixels = rContext.iTexturePixels;
	const WorldDimensions& rDimensions = rContext.rDimensions;
	const std::vector<float>& rFullElevationMeters = rBakeOutput.rFullElevationMeters;
	const std::vector<uint16_t>& rFullAmbientOcclusion = rBakeOutput.rFullAmbientOcclusion;
	const float fBeachOffsetMeters = rBakeOutput.fBeachOffsetMeters;
	const int64_t iRegionStartX = rRegion.iStartX;
	const int64_t iRegionEndX = rRegion.iEndX;
	const int64_t iRegionStartY = rRegion.iStartY;
	const int64_t iRegionEndY = rRegion.iEndY;
	const std::filesystem::path& rLeafDir = rLeaf.rLeafDir;
	const std::string& rTextureSourceDirRelative = rLeaf.rTextureSourceDirRelative;

	std::filesystem::path leafIntermediatesDir = rLeafDir / kpcIslandIntermediatesDir;

	// Auto-crop to the bbox of pixels above the sea-floor cut line, restricted to this region so a
	// 2x1 half never pulls land across the split seam. Cut line = -fBeachOffsetMeters +
	// kfCropEpsilonAboveSeaFloorMeters (epsilon meters up from the per-island sea floor). Empty
	// bbox is a configuration error: the route produced no terrain in this chunk.
	float fCropCutLineMeters = -fBeachOffsetMeters + kfCropEpsilonAboveSeaFloorMeters;
	int64_t iMinX = iRegionEndX;
	int64_t iMinY = iRegionEndY;
	int64_t iMaxX = -1;
	int64_t iMaxY = -1;
	for (int64_t iY = iRegionStartY; iY < iRegionEndY; ++iY)
	{
		const float* pfRow = &rFullElevationMeters.at(static_cast<size_t>(iY) * static_cast<size_t>(iTexturePixels));
		for (int64_t iX = iRegionStartX; iX < iRegionEndX; ++iX)
		{
			if (pfRow[iX] > fCropCutLineMeters)
			{
				iMinX = std::min(iMinX, iX);
				iMaxX = std::max(iMaxX, iX);
				iMinY = std::min(iMinY, iY);
				iMaxY = std::max(iMaxY, iY);
			}
		}
	}
	if (iMaxX < 0)
	{
		throw std::runtime_error(std::format("Island chunk \"{}\" region [{}..{}, {}..{}] has no pixels above the sea-floor cut line ({:.2f} m): the Route subdivision produced no terrain in this chunk. Check the archetype's Route shape or raise Island.json's elevationMeters.", rLeafDir.string(), iRegionStartX, iRegionEndX - 1, iRegionStartY, iRegionEndY - 1, fCropCutLineMeters));
	}

	// Expand bbox symmetrically to a multiple of kiCropAlignment per axis, clamped to the FULL bake
	// [0, iTexturePixels) -- NOT the region. When a region edge isn't 64-aligned (e.g. a 3-way split
	// of a power-of-two bake), the alignment padding borrows neighbour pixels across the seam to reach
	// the crop alignment; the borrowed strip is the inter-landmass gap (the bbox search already
	// confined this chunk's landmass to its own region). When clamping at the bake edge consumes one
	// side's padding, push the remainder onto the opposite side. iTexturePixels is itself a multiple of
	// kiCropAlignment, so the worst case (a 1x1 island spanning the whole bake) needs no padding and
	// never overflows.
	auto ExpandSpan = [](int64_t iLo, int64_t iHi, int64_t iClampLo, int64_t iClampHi, int64_t& riStart, int64_t& riSize)
	{
		int64_t iSpan = iHi - iLo + 1;
		int64_t iRequired = ((iSpan + kiCropAlignment - 1) / kiCropAlignment) * kiCropAlignment;
		int64_t iPad = iRequired - iSpan;
		int64_t iPadLow = iPad / 2;
		int64_t iPadHigh = iPad - iPadLow;
		int64_t iStart = iLo - iPadLow;
		int64_t iEnd = iHi + iPadHigh;
		if (iStart < iClampLo)
		{
			iEnd += iClampLo - iStart;
			iStart = iClampLo;
		}
		if (iEnd > iClampHi)
		{
			iStart -= iEnd - iClampHi;
			iEnd = iClampHi;
		}
		riStart = iStart;
		riSize = iEnd - iStart + 1;
	};

	int64_t iCropX = 0;
	int64_t iCropY = 0;
	int64_t iCropWidth = 0;
	int64_t iCropHeight = 0;
	ExpandSpan(iMinX, iMaxX, 0, iTexturePixels - 1, iCropX, iCropWidth);
	ExpandSpan(iMinY, iMaxY, 0, iTexturePixels - 1, iCropY, iCropHeight);
	LOG(kDefault, kDebug, "Cropping island chunk \"{}\": bbox ({}..{},{}..{}) -> ({}+{},{}+{}) [aligned to {}]", rLeafDir.string(), iMinX, iMaxX, iMinY, iMaxY, iCropX, iCropWidth, iCropY, iCropHeight, kiCropAlignment);

	// Crop elevation to iCropWidth × iCropHeight, then box-filter downsample by kiElevationDivisor
	// and write the leaf Elevation.r32. Downsampled dims are multiples of 4 because crop dims are
	// multiples of 4 × kiElevationDivisor.
	int64_t iElevationWidth = iCropWidth / kiElevationDivisor;
	int64_t iElevationHeight = iCropHeight / kiElevationDivisor;
	float fOneOverBoxSize = 1.0f / static_cast<float>(kiElevationDivisor * kiElevationDivisor);
	std::vector<float> downsampledPixels(static_cast<size_t>(iElevationWidth) * static_cast<size_t>(iElevationHeight));
	for (int64_t iOutY = 0; iOutY < iElevationHeight; ++iOutY)
	{
		for (int64_t iOutX = 0; iOutX < iElevationWidth; ++iOutX)
		{
			float fSum = 0.0f;
			for (int64_t iDy = 0; iDy < kiElevationDivisor; ++iDy)
			{
				const float* pfRow = &rFullElevationMeters.at(static_cast<size_t>(iCropY + iOutY * kiElevationDivisor + iDy) * static_cast<size_t>(iTexturePixels) + static_cast<size_t>(iCropX + iOutX * kiElevationDivisor));
				for (int64_t iDx = 0; iDx < kiElevationDivisor; ++iDx)
				{
					fSum += pfRow[iDx];
				}
			}
			downsampledPixels.at(static_cast<size_t>(iOutY) * static_cast<size_t>(iElevationWidth) + static_cast<size_t>(iOutX)) = fSum * fOneOverBoxSize;
		}
	}

	// Reject very low / underwater leaves: the downsampled peak is the exact shipped data ExportIsland
	// reports as fMaxHeightMeters. Below the threshold, delete any prior committed leaf (intermediates
	// AND BC outputs) and write nothing, so no BakedDimensions.json is created -- ExportIsland::Handles
	// never claims it, producing no kIsland chunk and no orphan texture chunks. AreLeavesDirty treats
	// the now-absent leaf folder as intentionally skipped.
	float fMaxHeightMeters = *std::ranges::max_element(downsampledPixels);
	if (fMaxHeightMeters < kfMinIslandMaxHeightMeters)
	{
		std::filesystem::remove_all(rLeafDir);
		LOG(kDefault, kDebug, "Rejected island leaf \"{}\": max height {}m below minimum {}m", rLeafDir.string(), common::Wb(fMaxHeightMeters, 2), common::Wb(kfMinIslandMaxHeightMeters, 2));
		return false;
	}

	std::filesystem::create_directories(leafIntermediatesDir);
	{
		std::ofstream writeStream(leafIntermediatesDir / "Elevation.r32", std::ios::binary | std::ios::trunc);
		writeStream.write(reinterpret_cast<const char*>(downsampledPixels.data()), downsampledPixels.size() * sizeof(float));
	}

	// Crop AmbientOcclusion to the same bbox and write the leaf AmbientOcclusion.r16. Color.png and
	// Normals.exr stay full-res in the route Intermediates (no writer in Texture.cpp for either
	// format); ExportIsland crops their pixel data in-memory via this leaf's crop rect.
	{
		std::vector<uint16_t> aoCropped(static_cast<size_t>(iCropWidth) * static_cast<size_t>(iCropHeight));
		for (int64_t iY = 0; iY < iCropHeight; ++iY)
		{
			const uint16_t* puiSrc = &rFullAmbientOcclusion.at(static_cast<size_t>(iCropY + iY) * static_cast<size_t>(iTexturePixels) + static_cast<size_t>(iCropX));
			uint16_t* puiDst = &aoCropped.at(static_cast<size_t>(iY) * static_cast<size_t>(iCropWidth));
			std::memcpy(puiDst, puiSrc, static_cast<size_t>(iCropWidth) * sizeof(uint16_t));
		}
		std::ofstream writeStream(leafIntermediatesDir / "AmbientOcclusion.r16", std::ios::binary | std::ios::trunc);
		writeStream.write(reinterpret_cast<const char*>(aoCropped.data()), aoCropped.size() * sizeof(uint16_t));
	}

	// Crop the (already beach-subdivided) mesh to this chunk's bbox and re-center its local origin
	// on the post-crop center. iCropX/iCropWidth map to engine X (Gaea X-east, no flip);
	// iCropY/iCropHeight map to engine Y with a sign flip (engine Y north-positive, heightmap row 0
	// = north edge). Discard triangles fully outside the bbox; keep partial-cross triangles for
	// silhouette quality (for a 2x1 split, a triangle straddling the seam is kept by both halves —
	// the same partial-cross behavior the single-island crop already uses at every edge). After the
	// index buffer is compacted, repack the vertex buffer to drop orphans, then re-center.
	int64_t iDiscardedTriangles = 0;
	{
		const double dPixelsToMeters = static_cast<double>(rDimensions.fFootprintMeters) / static_cast<double>(iTexturePixels);
		const float fCropMinXMeters = static_cast<float>(static_cast<double>(iCropX)                          * dPixelsToMeters - 0.5 * rDimensions.fFootprintMeters);
		const float fCropMaxXMeters = static_cast<float>(static_cast<double>(iCropX + iCropWidth)             * dPixelsToMeters - 0.5 * rDimensions.fFootprintMeters);
		const float fCropMaxYMeters = static_cast<float>(0.5 * rDimensions.fFootprintMeters - static_cast<double>(iCropY)               * dPixelsToMeters);
		const float fCropMinYMeters = static_cast<float>(0.5 * rDimensions.fFootprintMeters - static_cast<double>(iCropY + iCropHeight) * dPixelsToMeters);
		const float fCropCenterXMeters = 0.5f * (fCropMinXMeters + fCropMaxXMeters);
		const float fCropCenterYMeters = 0.5f * (fCropMinYMeters + fCropMaxYMeters);

		auto VertexOutside = [&meshPositions, fCropMinXMeters, fCropMaxXMeters, fCropMinYMeters, fCropMaxYMeters](uint32_t iV) -> bool
		{
			float fX = meshPositions[static_cast<size_t>(iV) * 3 + 0];
			float fY = meshPositions[static_cast<size_t>(iV) * 3 + 1];
			return fX < fCropMinXMeters || fX > fCropMaxXMeters || fY < fCropMinYMeters || fY > fCropMaxYMeters;
		};

		std::vector<uint32_t> survivingIndices;
		survivingIndices.reserve(meshIndices.size());
		for (size_t i = 0; i + 2 < meshIndices.size(); i += 3)
		{
			uint32_t iA = meshIndices[i + 0];
			uint32_t iB = meshIndices[i + 1];
			uint32_t iC = meshIndices[i + 2];
			if (VertexOutside(iA) && VertexOutside(iB) && VertexOutside(iC))
			{
				++iDiscardedTriangles;
				continue;
			}
			survivingIndices.push_back(iA);
			survivingIndices.push_back(iB);
			survivingIndices.push_back(iC);
		}
		meshIndices = std::move(survivingIndices);

		// Defense-in-depth twin of the pixel-cut-line throw above: the pixel bbox guard catches the
		// common case (no land in region), but a future Mesher with gaps could pass the pixel check
		// and still emit no triangles inside the crop bbox. Catch the empty-mesh chunk here rather
		// than silently shipping an invisible island.
		if (meshIndices.empty())
		{
			throw std::runtime_error(std::format("Island chunk \"{}\" region [{}..{}, {}..{}] has zero surviving triangles after mesh crop ({} discarded): the Route subdivision produced no mesh inside this chunk's bbox. Check the archetype's Mesher resolution or Route shape.", rLeafDir.string(), iRegionStartX, iRegionEndX - 1, iRegionStartY, iRegionEndY - 1, iDiscardedTriangles));
		}

		// Repack vertex buffer: walk indices to mark used vertices, then compact and remap.
		const int64_t iOldVertexCount = static_cast<int64_t>(meshPositions.size() / 3);
		std::vector<uint32_t> oldToNew(static_cast<size_t>(iOldVertexCount), UINT32_MAX);
		std::vector<float> packedPositions;
		packedPositions.reserve(meshPositions.size());
		for (uint32_t& riIndex : meshIndices)
		{
			uint32_t& riRemap = oldToNew[riIndex];
			if (riRemap == UINT32_MAX)
			{
				riRemap = static_cast<uint32_t>(packedPositions.size() / 3);
				packedPositions.push_back(meshPositions[static_cast<size_t>(riIndex) * 3 + 0]);
				packedPositions.push_back(meshPositions[static_cast<size_t>(riIndex) * 3 + 1]);
				packedPositions.push_back(meshPositions[static_cast<size_t>(riIndex) * 3 + 2]);
			}
			riIndex = riRemap;
		}
		meshPositions = std::move(packedPositions);

		// Re-center XY of every surviving vertex on the post-crop center. Z is unchanged
		// (Z=0 is sea level globally, independent of horizontal crop).
		for (size_t iV = 0; iV < meshPositions.size() / 3; ++iV)
		{
			meshPositions[iV * 3 + 0] -= fCropCenterXMeters;
			meshPositions[iV * 3 + 1] -= fCropCenterYMeters;
		}
		LOG(kDefault, kDebug, "Mesh chunk \"{}\": cropped {} triangles outside bbox, re-centered XY by ({:.2f}, {:.2f})m, {} -> {} vertices", rLeafDir.string(), iDiscardedTriangles, fCropCenterXMeters, fCropCenterYMeters, iOldVertexCount, static_cast<int64_t>(meshPositions.size() / 3));
	}

	{
		std::ofstream meshOut(leafIntermediatesDir / "MeshProcessed.bin", std::ios::binary | std::ios::trunc);
		int32_t iVertexCount32 = static_cast<int32_t>(meshPositions.size() / 3);
		int32_t iIndexCount32 = static_cast<int32_t>(meshIndices.size());
		meshOut.write(reinterpret_cast<const char*>(&iVertexCount32), sizeof(int32_t));
		meshOut.write(reinterpret_cast<const char*>(&iIndexCount32), sizeof(int32_t));
		meshOut.write(reinterpret_cast<const char*>(meshPositions.data()), static_cast<std::streamsize>(meshPositions.size() * sizeof(float)));
		meshOut.write(reinterpret_cast<const char*>(meshIndices.data()), static_cast<std::streamsize>(meshIndices.size() * sizeof(uint32_t)));
	}

	// BakedDimensions.json, written LAST in the leaf — its presence is the leaf-complete marker
	// ExportIsland::Handles keys on. Anisotropic post-crop world dims (meters-per-pixel is global,
	// so the formula is unchanged from the single-island case), the crop rect into the full bake,
	// and the leaf-relative path to the shared texture sources.
	{
		nlohmann::json bakedJson;
		bakedJson["widthMeters"] = rDimensions.fFootprintMeters * static_cast<float>(iCropWidth) / static_cast<float>(iTexturePixels);
		bakedJson["heightMeters"] = rDimensions.fFootprintMeters * static_cast<float>(iCropHeight) / static_cast<float>(iTexturePixels);
		bakedJson["elevationMeters"] = rDimensions.fElevationMeters;
		bakedJson["cropX"] = iCropX;
		bakedJson["cropY"] = iCropY;
		bakedJson["cropWidth"] = iCropWidth;
		bakedJson["cropHeight"] = iCropHeight;
		bakedJson["fullTexturePixels"] = iTexturePixels;
		bakedJson["textureSourceDir"] = rTextureSourceDirRelative;
		std::ofstream bakedStream(leafIntermediatesDir / kpcBakedDimensionsFile);
		bakedStream << bakedJson.dump(4);
	}

	return true;
}
