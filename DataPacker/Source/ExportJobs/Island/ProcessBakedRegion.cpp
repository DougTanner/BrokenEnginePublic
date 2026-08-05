#include "BakeIslandIntermediatesInternal.h"

// Crops one chunk region [xStart,xEnd) x [yStart,yEnd) out of the full Gaea bake and writes the
// chunk's per-region geometry (Elevation.r32, AmbientOcclusion.r16, MeshProcessed.bin,
// BakedDimensions.json) into the leaf's Gaea cache folder. The source leaf remains the home of the
// tracked BC outputs and provides ExportIsland's stable chunk path. Shared full-res
// Color/Normals/mask PNGs remain in the route cache, and the crop rect recorded in BakedDimensions
// lets ExportIsland crop them in-memory. A 1x1 route passes the
// whole texture as the region; a 2x1 route calls this once per Y-half, a 2x2 route once per quadrant
// (both axes split at the midpoints), so the existing auto-crop runs (and, in ExportIsland, the
// underwater re-coloring) once per chunk.
// meshPositions / meshIndices are taken by value so each region mutates its own copy of the shared
// post-subdivision mesh.
// Returns true if the leaf was written, false if rejected (too low — see kfMinIslandMaxHeightMeters).

namespace
{

// Widest edge-taper band, in meters. Matches the band the removed full-bake taper used (8192 / 16
// pixels at ~0.049 m/px). The quarter-dimension clamp in TaperLeafElevationEdgesToSeaFloor keeps a
// small leaf (down to ~50 m) from having its entire shoal deepened by a fixed-width band.
constexpr float kfEdgeTaperMaxMeters = 25.0f;

// Aligned crop rect into the full bake, in full-bake pixel coords. Shared output of the auto-crop
// stages and input to every later crop / downsample / dimension stage.
struct CropRect
{
	int64_t iX;
	int64_t iY;
	int64_t iWidth;
	int64_t iHeight;
};

// Auto-crop bbox of pixels above the sea-floor cut line, in full-bake pixel coords. iMaxX < 0 marks
// an empty bbox (the region produced no terrain) — the caller turns that into a configuration error.
struct RegionBbox
{
	int64_t iMinX;
	int64_t iMinY;
	int64_t iMaxX;
	int64_t iMaxY;
};

// Expand bbox span [iLo, iHi] symmetrically to a multiple of kiCropAlignment, clamped to
// [iClampLo, iClampHi]; when clamping at one edge consumes that side's padding, push the remainder
// onto the opposite side. Outputs the aligned start and size.
void ExpandSpan(int64_t iLo, int64_t iHi, int64_t iClampLo, int64_t iClampHi, int64_t& riStart, int64_t& riSize)
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
}

// Auto-crop to the bbox of pixels above the sea-floor cut line, restricted to this region so a
// 2x1 half never pulls land across the split seam. Cut line = -fBeachOffsetMeters +
// kfCropEpsilonAboveSeaFloorMeters (epsilon meters up from the per-island sea floor). An empty
// bbox (iMaxX < 0) is left for the caller to report as a configuration error.
RegionBbox FindRegionBbox(const std::vector<float>& rFullElevationMeters, int64_t iTexturePixels, const RegionBounds& rRegion, float fBeachOffsetMeters)
{
	float fCropCutLineMeters = -fBeachOffsetMeters + kfCropEpsilonAboveSeaFloorMeters;
	RegionBbox bbox {.iMinX = rRegion.iEndX, .iMinY = rRegion.iEndY, .iMaxX = -1, .iMaxY = -1};
	for (int64_t iY = rRegion.iStartY; iY < rRegion.iEndY; ++iY)
	{
		const float* pfRow = &rFullElevationMeters.at(static_cast<size_t>(iY) * static_cast<size_t>(iTexturePixels));
		for (int64_t iX = rRegion.iStartX; iX < rRegion.iEndX; ++iX)
		{
			if (pfRow[iX] > fCropCutLineMeters)
			{
				bbox.iMinX = std::min(bbox.iMinX, iX);
				bbox.iMaxX = std::max(bbox.iMaxX, iX);
				bbox.iMinY = std::min(bbox.iMinY, iY);
				bbox.iMaxY = std::max(bbox.iMaxY, iY);
			}
		}
	}
	return bbox;
}

// Expand bbox symmetrically to a multiple of kiCropAlignment per axis, clamped to the FULL bake
// [0, iTexturePixels) -- NOT the region. When a region edge isn't 64-aligned (e.g. a 3-way split
// of a power-of-two bake), the alignment padding borrows neighbour pixels across the seam to reach
// the crop alignment; the borrowed strip is the inter-landmass gap (the bbox search already
// confined this chunk's landmass to its own region). When clamping at the bake edge consumes one
// side's padding, push the remainder onto the opposite side. iTexturePixels is itself a multiple of
// kiCropAlignment, so the worst case (a 1x1 island spanning the whole bake) needs no padding and
// never overflows.
CropRect ComputeCropRect(const RegionBbox& rBbox, int64_t iTexturePixels)
{
	CropRect crop {.iX = 0, .iY = 0, .iWidth = 0, .iHeight = 0};
	ExpandSpan(rBbox.iMinX, rBbox.iMaxX, 0, iTexturePixels - 1, crop.iX, crop.iWidth);
	ExpandSpan(rBbox.iMinY, rBbox.iMaxY, 0, iTexturePixels - 1, crop.iY, crop.iHeight);
	return crop;
}

// Crop elevation to iCropWidth × iCropHeight, then box-filter downsample by kiElevationDivisor.
// Downsampled dims are multiples of 4 because crop dims are multiples of 4 × kiElevationDivisor.
std::vector<float> CropAndDownsampleElevation(const std::vector<float>& rFullElevationMeters, int64_t iTexturePixels, const CropRect& rCrop)
{
	int64_t iElevationWidth = rCrop.iWidth / kiElevationDivisor;
	int64_t iElevationHeight = rCrop.iHeight / kiElevationDivisor;
	float fOneOverBoxSize = 1.0f / static_cast<float>(kiElevationDivisor * kiElevationDivisor);
	std::vector<float> downsampledPixels(static_cast<size_t>(iElevationWidth) * static_cast<size_t>(iElevationHeight));
	for (int64_t iOutY = 0; iOutY < iElevationHeight; ++iOutY)
	{
		for (int64_t iOutX = 0; iOutX < iElevationWidth; ++iOutX)
		{
			float fSum = 0.0f;
			for (int64_t iDy = 0; iDy < kiElevationDivisor; ++iDy)
			{
				const float* pfRow = &rFullElevationMeters.at(static_cast<size_t>(rCrop.iY + iOutY * kiElevationDivisor + iDy) * static_cast<size_t>(iTexturePixels) + static_cast<size_t>(rCrop.iX + iOutX * kiElevationDivisor));
				for (int64_t iDx = 0; iDx < kiElevationDivisor; ++iDx)
				{
					fSum += pfRow[iDx];
				}
			}
			downsampledPixels.at(static_cast<size_t>(iOutY) * static_cast<size_t>(iElevationWidth) + static_cast<size_t>(iOutX)) = fSum * fOneOverBoxSize;
		}
	}
	return downsampledPixels;
}

// Force the leaf's border elevation smoothly down to the per-island sea floor. Every leaf ships as
// an independent island sitting in the engine's constant open-ocean elevation clear, so the taper
// runs per leaf — including edges produced by interior split cut lines, which the full bake's own
// border never covers. Water at or below halfway depth must reach exactly -fBeachOffsetMeters
// (= common::kfSeaBottomMeters) at the edge for it to blend with that clear, but shallows above
// halfway depth are preserved almost unchanged so the taper cannot amputate a leaf's visible sand
// apron where the coast nears a cut edge. Only underwater pixels are touched and none is ever
// raised, so terrain at or above sea level keeps its shape and terrain already deeper than the
// ceiling keeps its detail.
void TaperLeafElevationEdgesToSeaFloor(std::vector<float>& rDownsampledPixels, int64_t iWidth, int64_t iHeight, float fMetersPerPixel, float fBeachOffsetMeters)
{
	float fBandMeters = std::min(kfEdgeTaperMaxMeters, 0.25f * fMetersPerPixel * static_cast<float>(std::min(iWidth, iHeight)));
	if (fBandMeters <= 0.0f)
	{
		return;
	}

	float fSeaFloorMeters = -fBeachOffsetMeters;
	float fHalfwayMeters = 0.5f * fSeaFloorMeters;
	for (int64_t iY = 0; iY < iHeight; ++iY)
	{
		for (int64_t iX = 0; iX < iWidth; ++iX)
		{
			float fEdgeDistanceMeters = static_cast<float>(std::min({iX, iY, iWidth - 1 - iX, iHeight - 1 - iY})) * fMetersPerPixel;
			if (fEdgeDistanceMeters >= fBandMeters)
			{
				continue;
			}

			float fS = fEdgeDistanceMeters / fBandMeters;
			float fT = fS * fS * (3.0f - 2.0f * fS);
			float fCeiling = fSeaFloorMeters + fT * (0.0f - fSeaFloorMeters);

			float& rfPixel = rDownsampledPixels[static_cast<size_t>(iY) * static_cast<size_t>(iWidth) + static_cast<size_t>(iX)];
			if (rfPixel < 0.0f)
			{
				float fDepthWeight = std::clamp(rfPixel / fHalfwayMeters, 0.0f, 1.0f);
				fDepthWeight = fDepthWeight * fDepthWeight * (3.0f - 2.0f * fDepthWeight);
				rfPixel += fDepthWeight * (std::min(rfPixel, fCeiling) - rfPixel);
			}
		}
	}
}

// Write the downsampled leaf Elevation.r32.
void WriteElevation(const std::filesystem::path& rLeafIntermediatesDir, const std::vector<float>& rDownsampledPixels)
{
	std::ofstream writeStream(rLeafIntermediatesDir / "Elevation.r32", std::ios::binary | std::ios::trunc);
	writeStream.write(reinterpret_cast<const char*>(rDownsampledPixels.data()), rDownsampledPixels.size() * sizeof(float));
	writeStream.close();
	VERIFY_SUCCESS(writeStream.good());
}

// Crop AmbientOcclusion to the same bbox and write the leaf AmbientOcclusion.r16. Color.png and
// Normals.exr stay full-res in the route cache (no writer in Texture.cpp for either
// format); ExportIsland crops their pixel data in-memory via this leaf's crop rect.
void CropAndWriteAmbientOcclusion(const std::filesystem::path& rLeafIntermediatesDir, const std::vector<uint16_t>& rFullAmbientOcclusion, int64_t iTexturePixels, const CropRect& rCrop)
{
	std::vector<uint16_t> aoCropped(static_cast<size_t>(rCrop.iWidth) * static_cast<size_t>(rCrop.iHeight));
	for (int64_t iY = 0; iY < rCrop.iHeight; ++iY)
	{
		const uint16_t* puiSrc = &rFullAmbientOcclusion.at(static_cast<size_t>(rCrop.iY + iY) * static_cast<size_t>(iTexturePixels) + static_cast<size_t>(rCrop.iX));
		uint16_t* puiDst = &aoCropped.at(static_cast<size_t>(iY) * static_cast<size_t>(rCrop.iWidth));
		std::memcpy(puiDst, puiSrc, static_cast<size_t>(rCrop.iWidth) * sizeof(uint16_t));
	}
	std::ofstream writeStream(rLeafIntermediatesDir / "AmbientOcclusion.r16", std::ios::binary | std::ios::trunc);
	writeStream.write(reinterpret_cast<const char*>(aoCropped.data()), aoCropped.size() * sizeof(uint16_t));
	writeStream.close();
	VERIFY_SUCCESS(writeStream.good());
}

// Crop the (already beach-subdivided) mesh to this chunk's bbox and re-center its local origin
// on the post-crop center. iCropX/iCropWidth map to engine X (Gaea X-east, no flip);
// iCropY/iCropHeight map to engine Y with a sign flip (engine Y north-positive, heightmap row 0
// = north edge). Discard triangles fully outside the bbox; keep partial-cross triangles for
// silhouette quality (for a 2x1 split, a triangle straddling the seam is kept by both halves —
// the same partial-cross behavior the single-island crop already uses at every edge). After the
// index buffer is compacted, repack the vertex buffer to drop orphans, then re-center. Mutates the
// caller's by-value mesh copies in place.
void CropAndRepackMesh(std::vector<float>& rMeshPositions, std::vector<uint32_t>& rMeshIndices, const WorldDimensions& rDimensions, int64_t iTexturePixels, const CropRect& rCrop, const RegionBounds& rRegion, const std::filesystem::path& rLeafDir)
{
	int64_t iDiscardedTriangles = 0;
	const double dPixelsToMeters = static_cast<double>(rDimensions.fFootprintMeters) / static_cast<double>(iTexturePixels);
	const float fCropMinXMeters = static_cast<float>(static_cast<double>(rCrop.iX)                          * dPixelsToMeters - 0.5 * rDimensions.fFootprintMeters);
	const float fCropMaxXMeters = static_cast<float>(static_cast<double>(rCrop.iX + rCrop.iWidth)           * dPixelsToMeters - 0.5 * rDimensions.fFootprintMeters);
	const float fCropMaxYMeters = static_cast<float>(0.5 * rDimensions.fFootprintMeters - static_cast<double>(rCrop.iY)               * dPixelsToMeters);
	const float fCropMinYMeters = static_cast<float>(0.5 * rDimensions.fFootprintMeters - static_cast<double>(rCrop.iY + rCrop.iHeight) * dPixelsToMeters);
	const float fCropCenterXMeters = 0.5f * (fCropMinXMeters + fCropMaxXMeters);
	const float fCropCenterYMeters = 0.5f * (fCropMinYMeters + fCropMaxYMeters);

	auto VertexOutside = [&rMeshPositions, fCropMinXMeters, fCropMaxXMeters, fCropMinYMeters, fCropMaxYMeters](uint32_t iV) -> bool
	{
		float fX = rMeshPositions[static_cast<size_t>(iV) * 3 + 0];
		float fY = rMeshPositions[static_cast<size_t>(iV) * 3 + 1];
		return fX < fCropMinXMeters || fX > fCropMaxXMeters || fY < fCropMinYMeters || fY > fCropMaxYMeters;
	};

	std::vector<uint32_t> survivingIndices;
	survivingIndices.reserve(rMeshIndices.size());
	for (size_t i = 0; i + 2 < rMeshIndices.size(); i += 3)
	{
		uint32_t iA = rMeshIndices[i + 0];
		uint32_t iB = rMeshIndices[i + 1];
		uint32_t iC = rMeshIndices[i + 2];
		if (VertexOutside(iA) && VertexOutside(iB) && VertexOutside(iC))
		{
			++iDiscardedTriangles;
			continue;
		}
		survivingIndices.push_back(iA);
		survivingIndices.push_back(iB);
		survivingIndices.push_back(iC);
	}
	rMeshIndices = std::move(survivingIndices);

	// Defense-in-depth twin of the pixel-cut-line throw above: the pixel bbox guard catches the
	// common case (no land in region), but a future Mesher with gaps could pass the pixel check
	// and still emit no triangles inside the crop bbox. Catch the empty-mesh chunk here rather
	// than silently shipping an invisible island.
	if (rMeshIndices.empty())
	{
		throw std::runtime_error(std::format("Island chunk \"{}\" region [{}..{}, {}..{}] has zero surviving triangles after mesh crop ({} discarded): the Route subdivision produced no mesh inside this chunk's bbox. Check the archetype's Mesher resolution or Route shape.", rLeafDir.string(), rRegion.iStartX, rRegion.iEndX - 1, rRegion.iStartY, rRegion.iEndY - 1, iDiscardedTriangles));
	}

	// Compact + cache-optimize the vertex buffer: meshopt_optimizeVertexFetch reorders surviving
	// vertices into index-access order (GPU fetch efficiency) and drops orphans left by the crop,
	// rewriting rMeshIndices in place. Positions are bare float XYZ triples (12-byte stride).
	const size_t uiOldVertexCount = rMeshPositions.size() / 3;
	std::vector<float> packedPositions(rMeshPositions.size());
	size_t uiNewVertexCount = meshopt_optimizeVertexFetch(packedPositions.data(), rMeshIndices.data(), rMeshIndices.size(), rMeshPositions.data(), uiOldVertexCount, sizeof(float) * 3);
	packedPositions.resize(uiNewVertexCount * 3);
	rMeshPositions = std::move(packedPositions);

	// Re-center XY of every surviving vertex on the post-crop center. Z is unchanged
	// (Z=0 is sea level globally, independent of horizontal crop).
	for (size_t iV = 0; iV < rMeshPositions.size() / 3; ++iV)
	{
		rMeshPositions[iV * 3 + 0] -= fCropCenterXMeters;
		rMeshPositions[iV * 3 + 1] -= fCropCenterYMeters;
	}
	LOG(kDefault, kDebug, "Mesh chunk \"{}\": cropped {} triangles outside bbox, re-centered XY by ({:.2f}, {:.2f})m, {} -> {} vertices", rLeafDir.string(), iDiscardedTriangles, fCropCenterXMeters, fCropCenterYMeters, uiOldVertexCount, static_cast<int64_t>(rMeshPositions.size() / 3));
}

// Write the cropped / repacked / re-centered mesh to the leaf MeshProcessed.bin.
void WriteMeshProcessed(const std::filesystem::path& rLeafIntermediatesDir, const std::vector<float>& rMeshPositions, const std::vector<uint32_t>& rMeshIndices)
{
	std::ofstream meshOut(rLeafIntermediatesDir / "MeshProcessed.bin", std::ios::binary | std::ios::trunc);
	int32_t iVertexCount32 = static_cast<int32_t>(rMeshPositions.size() / 3);
	int32_t iIndexCount32 = static_cast<int32_t>(rMeshIndices.size());
	meshOut.write(reinterpret_cast<const char*>(&iVertexCount32), sizeof(int32_t));
	meshOut.write(reinterpret_cast<const char*>(&iIndexCount32), sizeof(int32_t));
	meshOut.write(reinterpret_cast<const char*>(rMeshPositions.data()), static_cast<std::streamsize>(rMeshPositions.size() * sizeof(float)));
	meshOut.write(reinterpret_cast<const char*>(rMeshIndices.data()), static_cast<std::streamsize>(rMeshIndices.size() * sizeof(uint32_t)));
	meshOut.close();
	VERIFY_SUCCESS(meshOut.good());
}

// BakedDimensions.json, written LAST in the leaf — its presence is the leaf-complete marker
// ExportIsland::Handles keys on. Anisotropic post-crop world dims (meters-per-pixel is global,
// so the formula is unchanged from the single-island case), the crop rect into the full bake,
// into the full bake. The shared texture source path is derived from the cache layout.
void WriteBakedDimensions(const std::filesystem::path& rLeafIntermediatesDirectory, const WorldDimensions& rDimensions, int64_t iTexturePixels, const CropRect& rCrop)
{
	nlohmann::json bakedJson;
	bakedJson["widthMeters"] = rDimensions.fFootprintMeters * static_cast<float>(rCrop.iWidth) / static_cast<float>(iTexturePixels);
	bakedJson["heightMeters"] = rDimensions.fFootprintMeters * static_cast<float>(rCrop.iHeight) / static_cast<float>(iTexturePixels);
	bakedJson["elevationMeters"] = rDimensions.fElevationMeters;
	bakedJson["cropX"] = rCrop.iX;
	bakedJson["cropY"] = rCrop.iY;
	bakedJson["cropWidth"] = rCrop.iWidth;
	bakedJson["cropHeight"] = rCrop.iHeight;
	bakedJson["fullTexturePixels"] = iTexturePixels;
	std::ofstream bakedStream(rLeafIntermediatesDirectory / kpcBakedDimensionsFile);
	bakedStream << bakedJson.dump(4);
	bakedStream.close();
	VERIFY_SUCCESS(bakedStream.good());
}

} // namespace

bool ProcessBakedRegion(const IslandBakeContext& rContext, const BakeOutput& rBakeOutput, const RegionBounds& rRegion, std::vector<float> meshPositions, std::vector<uint32_t> meshIndices, const LeafTarget& rLeaf)
{
	const std::filesystem::path& rSourceLeafDirectory = rLeaf.rSourceLeafDirectory;
	const std::filesystem::path& rCacheLeafDirectory = rLeaf.rCacheLeafDirectory;

	// Auto-crop bbox of pixels above the sea-floor cut line, confined to this region. An empty bbox
	// is a configuration error: the route produced no terrain in this chunk.
	RegionBbox bbox = FindRegionBbox(rBakeOutput.rFullElevationMeters, rContext.iTexturePixels, rRegion, rBakeOutput.fBeachOffsetMeters);
	if (bbox.iMaxX < 0)
	{
		float fCropCutLineMeters = -rBakeOutput.fBeachOffsetMeters + kfCropEpsilonAboveSeaFloorMeters;
		throw std::runtime_error(std::format("Island chunk \"{}\" region [{}..{}, {}..{}] has no pixels above the sea-floor cut line ({:.2f} m): the Route subdivision produced no terrain in this chunk. Check the archetype's Route shape or raise Island.json's elevationMeters.", rSourceLeafDirectory.string(), rRegion.iStartX, rRegion.iEndX - 1, rRegion.iStartY, rRegion.iEndY - 1, fCropCutLineMeters));
	}

	// Align and expand the bbox to the crop rect (multiple of kiCropAlignment per axis).
	CropRect crop = ComputeCropRect(bbox, rContext.iTexturePixels);
	LOG(kDefault, kDebug, "Cropping island chunk \"{}\": bbox ({}..{},{}..{}) -> ({}+{},{}+{}) [aligned to {}]", rSourceLeafDirectory.string(), bbox.iMinX, bbox.iMaxX, bbox.iMinY, bbox.iMaxY, crop.iX, crop.iWidth, crop.iY, crop.iHeight, kiCropAlignment);

	// Crop + box-filter downsample elevation, then reject very low / underwater leaves: the
	// downsampled peak is the exact shipped data ExportIsland reports as fMaxHeightMeters. Below the
	// threshold, delete any prior committed leaf (intermediates AND BC outputs) and write nothing, so
	// no BakedDimensions.json is created -- ExportIsland::Handles never claims it, producing no kIsland
	// chunk and no orphan texture chunks. AreLeavesDirty treats the now-absent leaf folder as
	// intentionally skipped.
	std::vector<float> downsampledPixels = CropAndDownsampleElevation(rBakeOutput.rFullElevationMeters, rContext.iTexturePixels, crop);
	float fMaxHeightMeters = *std::ranges::max_element(downsampledPixels);
	if (fMaxHeightMeters < kfMinIslandMaxHeightMeters)
	{
		std::filesystem::remove_all(rSourceLeafDirectory);
		std::filesystem::remove_all(rCacheLeafDirectory);
		std::filesystem::remove_all(GetIslandDiagnosticsPath(rSourceLeafDirectory));
		LOG(kDefault, kDebug, "Rejected island leaf \"{}\": max height {}m below minimum {}m", rSourceLeafDirectory.string(), common::Wb(fMaxHeightMeters, 2), common::Wb(kfMinIslandMaxHeightMeters, 2));
		return false;
	}

	// Taper after the rejection check (which judges the untapered peak) — the taper only ever lowers
	// pixels, so it cannot change that decision — and before the write, so the leaf Elevation.r32 and
	// everything derived from it (chunk payload, underwater texture masking, valid-area hull) stay in
	// lockstep.
	int64_t iElevationWidth = crop.iWidth / kiElevationDivisor;
	int64_t iElevationHeight = crop.iHeight / kiElevationDivisor;
	float fMetersPerPixel = rContext.rDimensions.fFootprintMeters / static_cast<float>(rContext.iTexturePixels) * static_cast<float>(kiElevationDivisor);
	TaperLeafElevationEdgesToSeaFloor(downsampledPixels, iElevationWidth, iElevationHeight, fMetersPerPixel, rBakeOutput.fBeachOffsetMeters);

	std::filesystem::create_directories(rSourceLeafDirectory);
	std::filesystem::create_directories(rCacheLeafDirectory);
	WriteElevation(rCacheLeafDirectory, downsampledPixels);
	CropAndWriteAmbientOcclusion(rCacheLeafDirectory, rBakeOutput.rFullAmbientOcclusion, rContext.iTexturePixels, crop);

	CropAndRepackMesh(meshPositions, meshIndices, rContext.rDimensions, rContext.iTexturePixels, crop, rRegion, rSourceLeafDirectory);
	WriteMeshProcessed(rCacheLeafDirectory, meshPositions, meshIndices);

	WriteBakedDimensions(rCacheLeafDirectory, rContext.rDimensions, rContext.iTexturePixels, crop);

	return true;
}
