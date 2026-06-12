#include "ExportIsland.h"

#include "Island/BakeIslandIntermediates.h"
#include "Texture/Texture.h"

#include "stb/stb_image.h"

using enum common::ChunkFlags;

// ---------------------------------------------------------------------------
// Single-resolution island ingest
// ---------------------------------------------------------------------------

struct ExportedIsland
{
	std::vector<float> cpuHeightmapData;
	std::vector<float> cpuMeshPositions;   // float2 XY pairs in island-local meters (origin at center). Z is discarded — Terrain.vert re-derives it from the elevation sampler.
	std::vector<uint32_t> cpuMeshIndices;
	std::vector<float> cpuValidAreaVertices;   // float2 XY pairs in island-local meters (origin at center) — CCW convex hull of pixels at or above the underwater mask threshold.
	int32_t iHeightmapWidth = 0;
	int32_t iHeightmapHeight = 0;
	int32_t iMeshVertexCount = 0;
	int32_t iMeshIndexCount = 0;
	int32_t iValidAreaVertexCount = 0;
	float fWorldFootprintXMeters = 0.0f;
	float fWorldFootprintYMeters = 0.0f;
	float fWorldElevationMeters = 0.0f;
	float fMaxHeightMeters = 0.0f;
};

// Convex hull (Andrew's monotone chain) of the island's valid area — the pixels at or above
// common::kfUnderwaterMaskThresholdMeters, the same cut line the texture masking uses. Per heightmap
// row we keep only the leftmost and rightmost qualifying pixel: the hull of those row extremes equals
// the hull of all qualifying pixels (any interior same-row pixel is a convex combination of its two
// extremes), so candidates stay O(H) rather than O(W·H). Pixels map to island-local meters via
// GlobalElevation's UV convention (origin at center, +Y north), matching cpuMeshPositions' frame.
// Output (rOut.cpuValidAreaVertices) is a CCW hull as flat float XY pairs; fewer than 3 hull vertices
// yields an empty hull.
static void BuildValidAreaHull(ExportedIsland& rOut)
{
	int32_t iWidth = rOut.iHeightmapWidth;
	int32_t iHeight = rOut.iHeightmapHeight;
	float fFootprintX = rOut.fWorldFootprintXMeters;
	float fFootprintY = rOut.fWorldFootprintYMeters;

	auto PixelToLocal = [&](int32_t iX, int32_t iY)
	{
		float fLocalX = (static_cast<float>(iX) / static_cast<float>(iWidth - 1) - 0.5f) * fFootprintX;
		float fLocalY = (0.5f - static_cast<float>(iY) / static_cast<float>(iHeight - 1)) * fFootprintY;
		return XMFLOAT2 {fLocalX, fLocalY};
	};

	std::vector<XMFLOAT2> candidates;
	candidates.reserve(static_cast<size_t>(iHeight) * 2);
	for (int32_t iY = 0; iY < iHeight; ++iY)
	{
		int32_t iLeft = -1;
		int32_t iRight = -1;
		for (int32_t iX = 0; iX < iWidth; ++iX)
		{
			if (rOut.cpuHeightmapData[static_cast<size_t>(iY) * static_cast<size_t>(iWidth) + static_cast<size_t>(iX)] >= common::kfUnderwaterMaskThresholdMeters)
			{
				if (iLeft < 0)
				{
					iLeft = iX;
				}
				iRight = iX;
			}
		}
		if (iLeft < 0)
		{
			continue;
		}
		candidates.push_back(PixelToLocal(iLeft, iY));
		if (iRight != iLeft)
		{
			candidates.push_back(PixelToLocal(iRight, iY));
		}
	}

	if (candidates.size() < 3)
	{
		return;
	}

	std::sort(candidates.begin(), candidates.end(), [](const XMFLOAT2& rA, const XMFLOAT2& rB)
	{
		return rA.x < rB.x || (rA.x == rB.x && rA.y < rB.y);
	});

	// Cross product of (rA-rO) x (rB-rO); <= 0 drops collinear points, yielding a CCW hull.
	auto Cross = [](const XMFLOAT2& rO, const XMFLOAT2& rA, const XMFLOAT2& rB)
	{
		return (rA.x - rO.x) * (rB.y - rO.y) - (rA.y - rO.y) * (rB.x - rO.x);
	};

	int32_t iCount = static_cast<int32_t>(candidates.size());
	std::vector<XMFLOAT2> hull(static_cast<size_t>(iCount) * 2);
	int32_t iK = 0;
	for (int32_t i = 0; i < iCount; ++i)
	{
		while (iK >= 2 && Cross(hull[static_cast<size_t>(iK) - 2], hull[static_cast<size_t>(iK) - 1], candidates[static_cast<size_t>(i)]) <= 0.0f)
		{
			--iK;
		}
		hull[static_cast<size_t>(iK++)] = candidates[static_cast<size_t>(i)];
	}
	for (int32_t i = iCount - 2, iLower = iK + 1; i >= 0; --i)
	{
		while (iK >= iLower && Cross(hull[static_cast<size_t>(iK) - 2], hull[static_cast<size_t>(iK) - 1], candidates[static_cast<size_t>(i)]) <= 0.0f)
		{
			--iK;
		}
		hull[static_cast<size_t>(iK++)] = candidates[static_cast<size_t>(i)];
	}
	// Last point repeats the first; drop it.
	hull.resize(static_cast<size_t>(iK) - 1);

	if (hull.size() < 3)
	{
		return;
	}

	rOut.iValidAreaVertexCount = static_cast<int32_t>(hull.size());
	rOut.cpuValidAreaVertices.reserve(hull.size() * 2);
	for (const XMFLOAT2& rVert : hull)
	{
		rOut.cpuValidAreaVertices.push_back(rVert.x);
		rOut.cpuValidAreaVertices.push_back(rVert.y);
	}
}

static void ExportIslandData(const std::filesystem::path& rInputPath, ExportedIsland& rOut)
{
	// rInputPath is the chunk leaf folder (<island>/<route>/<index>). Per-chunk cropped geometry
	// (Elevation.r32, AmbientOcclusion.r16, MeshProcessed.bin, BakedDimensions.json) lives in the
	// leaf's own Intermediates/ folder; the route's chunks SHARE the full-res Color (PNG8 sRGB) /
	// Normals (EXR) / mask PNG sources in textureSourceDir (the route Intermediates dir, resolved
	// from baked.textureSourceDir = "../Intermediates"), cropped in-memory below via this leaf's crop
	// rect so each chunk's outputs and JPG sidecars land at its own crop dims. BakedDimensions.json
	// drives every downstream size; it is written last per leaf, so its presence is guaranteed if the
	// bake succeeded. The committed BC outputs are saved to the leaf root (not Intermediates).
	BakedDimensions baked = ReadBakedDimensions(rInputPath);
	std::filesystem::path intermediatesDir = rInputPath / kpcIslandIntermediatesDir;
	std::filesystem::path textureSourceDir = rInputPath / baked.textureSourceDir;
	rOut.fWorldFootprintXMeters = baked.fWidthMeters;
	rOut.fWorldFootprintYMeters = baked.fHeightMeters;
	rOut.fWorldElevationMeters = baked.fElevationMeters;
	int64_t iElevationWidth = baked.iCropWidth / kiElevationDivisor;
	int64_t iElevationHeight = baked.iCropHeight / kiElevationDivisor;

	// Read the downsampled engine-meter elevation up front so it can drive both the per-texture
	// underwater mask (below) and the chunk payload heightmap (further down). Same buffer, single
	// read.
	rOut.cpuHeightmapData.resize(static_cast<size_t>(iElevationWidth) * static_cast<size_t>(iElevationHeight));
	std::fstream rawStream(intermediatesDir / "Elevation.r32", std::ios::in | std::ios::binary);
	rawStream.read(reinterpret_cast<char*>(rOut.cpuHeightmapData.data()), rOut.cpuHeightmapData.size() * sizeof(float));
	rawStream.close();
	rOut.iHeightmapWidth = static_cast<int32_t>(iElevationWidth);
	rOut.iHeightmapHeight = static_cast<int32_t>(iElevationHeight);

	// Actual peak over the downsampled shipped heightmap — the exact data the runtime samples, so
	// the value matches drawn geometry. Values are engine-meters above beach (finite, non-empty).
	rOut.fMaxHeightMeters = *std::ranges::max_element(rOut.cpuHeightmapData);

	// Convex hull of the valid (above-threshold) region — same heightmap + threshold as the texture
	// masking. Packed into the chunk payload after the mesh (see Export()); debug render draws it.
	BuildValidAreaHull(rOut);

	// Runtime SAT (common::ConvexHullsOverlap) requires both inputs to be convex and CCW; verify the
	// producer here so a future change to BuildValidAreaHull (or a degenerate input) fails the bake
	// instead of silently letting islands intersect at the waterline.
	if (rOut.iValidAreaVertexCount >= 3)
	{
		float fSignedArea = 0.0f;
		for (int32_t i = 0; i < rOut.iValidAreaVertexCount; ++i)
		{
			int32_t iNext = (i + 1) % rOut.iValidAreaVertexCount;
			float fAx = rOut.cpuValidAreaVertices.at(static_cast<size_t>(i) * 2);
			float fAy = rOut.cpuValidAreaVertices.at(static_cast<size_t>(i) * 2 + 1);
			float fBx = rOut.cpuValidAreaVertices.at(static_cast<size_t>(iNext) * 2);
			float fBy = rOut.cpuValidAreaVertices.at(static_cast<size_t>(iNext) * 2 + 1);
			fSignedArea += fAx * fBy - fBx * fAy;
		}
		ASSERT(fSignedArea > 0.0f);
		for (int32_t i = 0; i < rOut.iValidAreaVertexCount; ++i)
		{
			int32_t iPrev = (i + rOut.iValidAreaVertexCount - 1) % rOut.iValidAreaVertexCount;
			int32_t iNext = (i + 1) % rOut.iValidAreaVertexCount;
			float fPx = rOut.cpuValidAreaVertices.at(static_cast<size_t>(iPrev) * 2);
			float fPy = rOut.cpuValidAreaVertices.at(static_cast<size_t>(iPrev) * 2 + 1);
			float fCx = rOut.cpuValidAreaVertices.at(static_cast<size_t>(i) * 2);
			float fCy = rOut.cpuValidAreaVertices.at(static_cast<size_t>(i) * 2 + 1);
			float fNx = rOut.cpuValidAreaVertices.at(static_cast<size_t>(iNext) * 2);
			float fNy = rOut.cpuValidAreaVertices.at(static_cast<size_t>(iNext) * 2 + 1);
			float fCross = (fCx - fPx) * (fNy - fPy) - (fCy - fPy) * (fNx - fPx);
			ASSERT(fCross > 0.0f);
		}
	}

	// Encode each intermediate as a BC-compressed mip chain in turn (MakeMipmaps walks down to the
	// BC 4-divisibility floor). sEncodeMutex serializes the BC encoder across textures (it spawns up to
	// HardwareCoreCount() - 2 threads internally; mutex bounds memory). A JPEG sidecar is written next to each
	// output for visual diagnosis of the bake input (mip 0 only). MaskByHeightmap runs before
	// MakeMipmaps on each so the flat-value underwater regions propagate down the mip chain via
	// the box / linear downsample naturally — the BC encoder then sees long constant runs at every
	// mip, and zlib catches the across-block repetition for free.
	static constexpr int kiJpegSidecarQuality = 90;
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(intermediatesDir / "AmbientOcclusion.r16", FileType::kUint16Raw, baked.iCropWidth, baked.iCropHeight);
		const float pfFlatAmbientOcclusion[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		texture.MaskByHeightmap(rOut.cpuHeightmapData, iElevationWidth, iElevationHeight, kiElevationDivisor, common::kfUnderwaterMaskThresholdMeters, pfFlatAmbientOcclusion);
		texture.MakeMipmaps(VK_FORMAT_BC4_UNORM_BLOCK, 32, {});
		texture.Save(rInputPath / kpcIslandAmbientOcclusion, VK_FORMAT_BC4_UNORM_BLOCK, {});
		texture.SaveJpegSidecar(rInputPath / "AmbientOcclusion.jpg", kiJpegSidecarQuality, TextureOptions::kGrayscale);
	}

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(textureSourceDir / "Color.png", FileType::kImage);
		texture.Crop(baked.iCropX, baked.iCropY, baked.iCropWidth, baked.iCropHeight);
		// Flat alpha stays 255 so BC7 keeps its alpha-free mode and the kVerifyNoAlpha assert
		// at Save still passes — RGB carries the underwater zero, alpha is invariant.
		const float pfFlatColor[4] = {0.0f, 0.0f, 0.0f, 255.0f};
		texture.MaskByHeightmap(rOut.cpuHeightmapData, iElevationWidth, iElevationHeight, kiElevationDivisor, common::kfUnderwaterMaskThresholdMeters, pfFlatColor);
		texture.MakeMipmaps(VK_FORMAT_BC7_UNORM_BLOCK, 32, {});
		texture.Save(rInputPath / kpcIslandColor, VK_FORMAT_BC7_UNORM_BLOCK, TextureOptions::kVerifyNoAlpha);
		texture.SaveJpegSidecar(rInputPath / "Color.jpg", kiJpegSidecarQuality, {});
	}

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(textureSourceDir / "Normals.exr", FileType::kExr);
		texture.Crop(baked.iCropX, baked.iCropY, baked.iCropWidth, baked.iCropHeight);
		// Flat (127.5, 127.5) → shader 2x-1 → (0, 0) → reconstructed Z=1 → flat tangent normal (0,0,1).
		const float pfFlatNormals[4] = {127.5f, 127.5f, 0.0f, 0.0f};
		texture.MaskByHeightmap(rOut.cpuHeightmapData, iElevationWidth, iElevationHeight, kiElevationDivisor, common::kfUnderwaterMaskThresholdMeters, pfFlatNormals);
		texture.MakeMipmaps(VK_FORMAT_BC5_UNORM_BLOCK, 32, {});
		texture.Save(rInputPath / kpcIslandNormals, VK_FORMAT_BC5_UNORM_BLOCK, {});
		texture.SaveJpegSidecar(rInputPath / "Normals.jpg", kiJpegSidecarQuality, {});
	}

	// Material masks: pack Rock / Sand / Snow / Flow PNGs into a single BC7 RGBA texture cropped to
	// match Color / Normals UVs, then 4x downsampled for ~25% of Color's footprint. R=Rock, G=Sand,
	// B=Snow, A=Flow (Flow channel reserved; Terrain.frag ignores it today). Source PNGs are 8-bit
	// palette grayscale at the same dimensions as Color.png (stb decodes the palette to luminance).
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);

		const char* pcMaskNames[4] = {"Rock.png", "Sand.png", "Snow.png", "Flow.png"};
		stbi_uc* ppMaskPixels[4] = {};
		int iMaskWidth = 0;
		int iMaskHeight = 0;
		common::ScopedLambda freeMaskPixels([&]()
		{
			for (stbi_uc* pPixels : ppMaskPixels)
			{
				if (pPixels != nullptr)
				{
					stbi_image_free(pPixels);
				}
			}
		});

		for (int64_t i = 0; i < 4; ++i)
		{
			int iWidth = 0;
			int iHeight = 0;
			int iChannelsInFile = 0;
			ppMaskPixels[i] = stbi_load((textureSourceDir / pcMaskNames[i]).string().c_str(), &iWidth, &iHeight, &iChannelsInFile, STBI_grey);
			ASSERT(ppMaskPixels[i] != nullptr);
			if (i == 0)
			{
				iMaskWidth = iWidth;
				iMaskHeight = iHeight;
			}
			else
			{
				ASSERT(iWidth == iMaskWidth && iHeight == iMaskHeight);
			}
		}

		std::vector<std::byte> packedRgba(static_cast<size_t>(iMaskWidth) * static_cast<size_t>(iMaskHeight) * 4);
		int64_t iPixelCount = static_cast<int64_t>(iMaskWidth) * static_cast<int64_t>(iMaskHeight);
		for (int64_t i = 0; i < iPixelCount; ++i)
		{
			packedRgba[static_cast<size_t>(i * 4 + 0)] = std::byte {ppMaskPixels[0][i]};
			packedRgba[static_cast<size_t>(i * 4 + 1)] = std::byte {ppMaskPixels[1][i]};
			packedRgba[static_cast<size_t>(i * 4 + 2)] = std::byte {ppMaskPixels[2][i]};
			packedRgba[static_cast<size_t>(i * 4 + 3)] = std::byte {ppMaskPixels[3][i]};
		}

		Texture texture(packedRgba.data(), iMaskWidth, iMaskHeight, 4);
		texture.Crop(baked.iCropX, baked.iCropY, baked.iCropWidth, baked.iCropHeight);
		// 4x reduction matches Elevation's downsample ratio; crop dims are multiples of 4*kiElevationDivisor=16
		// so the post-Downsize dims stay multiples of 4 for BC alignment.
		texture.Downsize(2);
		// Post-Downsize dims (cropW/4 × cropH/4) match the heightmap exactly, so divisor = 1. All four
		// mask channels go to zero underwater (no rock/sand/snow/flow override below the cut line).
		const float pfFlatMasks[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		texture.MaskByHeightmap(rOut.cpuHeightmapData, iElevationWidth, iElevationHeight, 1, common::kfUnderwaterMaskThresholdMeters, pfFlatMasks);
		texture.MakeMipmaps(VK_FORMAT_BC7_UNORM_BLOCK, 32, {});
		// kVerifyNoAlpha omitted: A channel carries real data (Flow mask).
		texture.Save(rInputPath / kpcIslandMasks, VK_FORMAT_BC7_UNORM_BLOCK, {});
		texture.SaveJpegSidecar(rInputPath / "Masks.jpg", kiJpegSidecarQuality, {});
	}

	// cpuHeightmapData / iHeightmapWidth / iHeightmapHeight were populated at the top of this
	// function so the underwater mask could share the buffer; nothing more to do for the heightmap
	// here — Export() packs it into the chunk payload below.

	// Per-island mesh: BakeIslandIntermediates wrote MeshProcessed.bin with [int32 vertexCount,
	// int32 indexCount, float3 positions, uint32 indices]. Strip Z here — Terrain.vert re-derives
	// it from the elevation sampler — and pack float2 XY pairs into the chunk payload after the
	// heightmap floats (see Export() and IslandHeader in DataFile.h).
	{
		std::filesystem::path meshFile = intermediatesDir / "MeshProcessed.bin";
		std::ifstream meshStream(meshFile, std::ios::binary);
		meshStream.read(reinterpret_cast<char*>(&rOut.iMeshVertexCount), sizeof(int32_t));
		meshStream.read(reinterpret_cast<char*>(&rOut.iMeshIndexCount), sizeof(int32_t));
		std::vector<float> meshPositionsXYZ(static_cast<size_t>(rOut.iMeshVertexCount) * 3);
		rOut.cpuMeshPositions.resize(static_cast<size_t>(rOut.iMeshVertexCount) * 2);
		rOut.cpuMeshIndices.resize(static_cast<size_t>(rOut.iMeshIndexCount));
		meshStream.read(reinterpret_cast<char*>(meshPositionsXYZ.data()), static_cast<std::streamsize>(meshPositionsXYZ.size() * sizeof(float)));
		meshStream.read(reinterpret_cast<char*>(rOut.cpuMeshIndices.data()), static_cast<std::streamsize>(rOut.cpuMeshIndices.size() * sizeof(uint32_t)));
		for (int32_t i = 0; i < rOut.iMeshVertexCount; ++i)
		{
			rOut.cpuMeshPositions.at(static_cast<size_t>(i) * 2)     = meshPositionsXYZ.at(static_cast<size_t>(i) * 3);
			rOut.cpuMeshPositions.at(static_cast<size_t>(i) * 2 + 1) = meshPositionsXYZ.at(static_cast<size_t>(i) * 3 + 1);
		}
	}
}

// ---------------------------------------------------------------------------
// ExportJob entry points
// ---------------------------------------------------------------------------

std::optional<common::ChunkFlags_t> ExportIsland::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	// A chunk leaf is <Islands>/<island>/<route>/<index>: it carries Intermediates/BakedDimensions.json
	// (written last per leaf by the bake). The route folder's own Intermediates/ holds the raw Gaea
	// bake but NO BakedDimensions.json, so gating on that file claims exactly the leaves (one kIsland
	// chunk each). The "Islands" ancestor guard keeps a stray match elsewhere from being mistaken for
	// a chunk.
	if (!rDirectoryEntry.is_directory() || !std::filesystem::exists(rDirectoryEntry.path() / kpcIslandIntermediatesDir / kpcBakedDimensionsFile))
	{
		return std::nullopt;
	}

	bool bUnderIslands = false;
	for (std::filesystem::path ancestor = rDirectoryEntry.path().parent_path(); !ancestor.empty(); )
	{
		if (ancestor.filename() == "Islands")
		{
			bUnderIslands = true;
			break;
		}
		std::filesystem::path parent = ancestor.parent_path();
		if (parent == ancestor)
		{
			break;
		}
		ancestor = parent;
	}
	return bUnderIslands ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kIsland) : std::nullopt;
}

bool ExportIsland::CheckDirty(const std::filesystem::path& rPackFile)
{
	if (ExportJob::CheckDirty(rPackFile))
	{
		return true;
	}

	// Base CheckDirty compares against the leaf directory's mtime, which does NOT propagate from
	// edits to the files inside it or to the shared route-level textures. Re-run the export whenever
	// any file in the leaf's own Intermediates/ (per-chunk geometry) OR the route's Intermediates/
	// dir (shared Color / Normals / mask sources, one level up) is newer than the chunk file, so a
	// fresh bake actually reaches the .pack / .jpg sidecars.
	std::filesystem::file_time_type chunkFileLastWriteTime = std::filesystem::last_write_time(mChunkFile);
	std::filesystem::path searchDirs[] = {mInputPath / kpcIslandIntermediatesDir, mInputPath.parent_path() / kpcIslandIntermediatesDir};
	for (const std::filesystem::path& rSearchDir : searchDirs)
	{
		if (!std::filesystem::exists(rSearchDir))
		{
			continue;
		}
		for (const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(rSearchDir))
		{
			if (rEntry.is_regular_file() && std::filesystem::last_write_time(rEntry.path()) > chunkFileLastWriteTime)
			{
				auto [date, time] = common::FileTimeString(std::filesystem::last_write_time(rEntry.path()));
				LOG(kDefault, kDebug, "Intermediate \"{}\" is newer than chunk \"{}\": {} {}", rEntry.path().string(), mChunkFile.string(), date, time);
				mbDirty = true;
				return mbDirty;
			}
		}
	}

	return false;
}

void ExportIsland::Export()
{
	ExportedIsland exported;
	ExportIslandData(mInputPath, exported);

	std::filesystem::path relativeFile = mRelativeDirectory;
	relativeFile /= mInputPath.filename();

	int64_t iHeightmapDataSize = static_cast<int64_t>(exported.cpuHeightmapData.size() * sizeof(float));
	int64_t iMeshPositionBytes = static_cast<int64_t>(exported.cpuMeshPositions.size() * sizeof(float));
	int64_t iMeshIndexBytes = static_cast<int64_t>(exported.cpuMeshIndices.size() * sizeof(uint32_t));
	int64_t iValidAreaBytes = static_cast<int64_t>(exported.cpuValidAreaVertices.size() * sizeof(float));
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iHeightmapDataSize + iMeshPositionBytes + iMeshIndexBytes + iValidAreaBytes);

	std::filesystem::path ambientOcclusionFile(relativeFile);
	ambientOcclusionFile /= kpcIslandAmbientOcclusion;
	pHeader->islandHeader.ambientOcclusionCrc = common::Crc(ambientOcclusionFile.string());

	std::filesystem::path colorsFile(relativeFile);
	colorsFile /= kpcIslandColor;
	pHeader->islandHeader.colorsCrc = common::Crc(colorsFile.string());

	std::filesystem::path normalsFile(relativeFile);
	normalsFile /= kpcIslandNormals;
	pHeader->islandHeader.normalsCrc = common::Crc(normalsFile.string());

	std::filesystem::path masksFile(relativeFile);
	masksFile /= kpcIslandMasks;
	pHeader->islandHeader.masksCrc = common::Crc(masksFile.string());

	pHeader->islandHeader.iHeightmapWidth = exported.iHeightmapWidth;
	pHeader->islandHeader.iHeightmapHeight = exported.iHeightmapHeight;
	pHeader->islandHeader.fWorldFootprintXMeters = exported.fWorldFootprintXMeters;
	pHeader->islandHeader.fWorldFootprintYMeters = exported.fWorldFootprintYMeters;
	pHeader->islandHeader.fWorldElevationMeters = exported.fWorldElevationMeters;
	pHeader->islandHeader.fMaxHeightMeters = exported.fMaxHeightMeters;
	pHeader->islandHeader.iMeshVertexCount = exported.iMeshVertexCount;
	pHeader->islandHeader.iMeshIndexCount = exported.iMeshIndexCount;
	pHeader->islandHeader.iValidAreaVertexCount = exported.iValidAreaVertexCount;

	// Chunk payload: [heightmap floats][mesh positions][mesh indices][valid-area hull verts]. Runtime
	// IslandTerrain slices these contiguously using IslandHeader's count fields.
	std::byte* pData = reinterpret_cast<std::byte*>(dataSpan.data());
	std::memcpy(pData, exported.cpuHeightmapData.data(), iHeightmapDataSize);
	std::memcpy(pData + iHeightmapDataSize, exported.cpuMeshPositions.data(), iMeshPositionBytes);
	std::memcpy(pData + iHeightmapDataSize + iMeshPositionBytes, exported.cpuMeshIndices.data(), iMeshIndexBytes);
	std::memcpy(pData + iHeightmapDataSize + iMeshPositionBytes + iMeshIndexBytes, exported.cpuValidAreaVertices.data(), iValidAreaBytes);
}
