#include "BakeIslandIntermediatesInternal.h"

#include "FileManager.h"
#include "GaeaArchetype.h"
#include "SubdivideBeachBand.h"

#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable: 5311) // nlohmann::json 3.10.4 uses the pre-C++20 literal-operator-id form 'operator "" _json'
#if defined(__clang__)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#endif
#include "tinygltf/json.hpp"
#include "tinygltf/tiny_gltf.h"
#if defined(__clang__)
	#pragma clang diagnostic pop
#endif
#pragma warning(pop)

namespace
{

// One Intermediates folder per route (folder name kpcIslandIntermediatesDir, see ExportIsland.h).
// One Gaea bake per route produces all six files below at texturePixels resolution; the raw
// Elevation.r32 stays as the bake source (it is NOT rewritten in place) — ProcessBakedRegion reads
// it and writes the per-chunk downsampled (texturePixels / kiElevationDivisor) elevation into each
// leaf's own Intermediates/. Elevation.r32 is headerless IEEE-754 float (Gaea's FloatRaw32 format),
// normalized [0,1] at bake time — DataPacker reads the Sea node Level from the archetype (fallback
// kfGaeaSeaLevelDefault when absent), then scales to meters by elevationMeters and subtracts
// `Level × elevationMeters` so beach = 0 in engine space and the sea floor sits at
// -(Level × elevationMeters) per island. AmbientOcclusion.r16 stays UshortRaw16 (precision-matched
// to BC4_UNORM). Color is 8-bit PNG (sRGB-encoded display data; downstream BC7 is 8-bit anyway, so
// EXR's float precision was wasted and its color-space convention conflicts with Gaea writing
// sRGB-encoded values into the EXR container). Normals stay multi-channel EXR. Files written by
// Gaea directly. Used for both the post-Gaea existence verification and the IsGaeaRawDirty
// existence check. These raw outputs live in each route's <route>/Intermediates/ folder.
constexpr const char* kpcIntermediateFiles[] =
{
	"AmbientOcclusion.r16",
	"Color.png",
	"Elevation.r32",
	"Normals.exr",
	"Mesh.gltf",  // Gaea Mesher output: glTF JSON manifest (separate-format)
	"Mesh.bin",   // Gaea Mesher output: binary buffer referenced by Mesh.gltf
};

// MeshProcessed.bin (positions + indices in island-local meters, XY-centered, sea-level Z=0) is
// derived per chunk leaf, not in the route's raw Intermediates — the per-leaf dirty check in
// AreLeavesDirty verifies it (and Elevation.r32 / AmbientOcclusion.r16 / BakedDimensions.json)
// exists in each chunk folder.

// Two-stage version sentinels, both route-level in <route>/Intermediates/. The bake separates the
// SLOW Gaea raw export from the FAST post-Gaea split so a split-only change (a kRouteSubdivisions
// columns/rows edit, or ProcessBakedRegion crop logic) re-splits from the existing Gaea output
// WITHOUT re-running Gaea.Swarm.
//
// kiBakeVersion (BakeVersion.txt): the Gaea RAW output. Bump only when something that changes the
// raw bake changes — the archetype patch (dims / seed / Route Choice / Mesher resolution) or the
// Gaea invocation. IsGaeaRawDirty re-runs Gaea on mismatch.
//
// kiSplitVersion (SplitVersion.txt): the post-Gaea split. Bump when ProcessBakedRegion or the chunk
// split (incl. kRouteSubdivisions columns/rows) changes. AreLeavesDirty re-splits from the existing
// raw on mismatch — no Gaea re-export.
constexpr int32_t kiBakeVersion = 28;
constexpr int32_t kiSplitVersion = 4;

// Beach-band adaptive subdivision constants. After the Gaea Mesher mesh is parsed, every triangle
// whose Z-range overlaps the beach band gets recursively split (1->4 midpoint) until its longest XY
// edge falls below the target. Out-of-band neighbors that inherit a midpoint via a shared edge get
// the minimal absorption split (1->2 for 1 midpoint, 1->3 for 2, true 1->4 for 3) -- introducing no
// new midpoints, so the cascade dies at one ring around the band. See plan
// `Documents/Plans/Graphics/BeachMeshSubdivide.md`.
// Band straddles beach (engine-Z = 0): triangles whose Z-range overlaps
// [kfBeachSubdivisionMinMeters, kfBeachSubdivisionMaxMeters] in absolute engine-meters densify,
// so both shallow water and just-above-beach terrain are covered. Independent of elevationMeters;
// set the two bounds asymmetrically to widen the underwater or above-water side independently.
constexpr float kfBeachSubdivisionMinMeters = -0.25f;
constexpr float kfBeachSubdivisionMaxMeters = 0.5f;
constexpr float kfBeachSubdivisionMaxEdgeMeters = 1.0f;
constexpr int32_t kiBeachSubdivisionMaxDepth = 12;

constexpr const char* kpcBakeVersionFile = "BakeVersion.txt";    // Gaea-raw stage sentinel (kiBakeVersion)
constexpr const char* kpcSplitVersionFile = "SplitVersion.txt";  // post-Gaea split stage sentinel (kiSplitVersion)
constexpr const char* kpcPatchedArchetypeFile = "PatchedArchetype.terrain";

// True if the route's RAW Gaea outputs are missing or stale — forces a (slow) Gaea.Swarm re-export.
// Checks only Gaea-output concerns: the raw intermediate files + the patched archetype (needed for
// the split's sea-level read) are present, the Gaea-bake-version sentinel matches, and the raw
// files are no older than Island.json / the archetype. The post-Gaea split is checked separately
// by AreLeavesDirty so a split-only change never trips this.
bool IsGaeaRawDirty(const std::filesystem::path& rIntermediatesDir, const std::filesystem::path& rIslandJsonFile, const std::filesystem::path& rArchetypeFile)
{
	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (!std::filesystem::exists(rIntermediatesDir / pcFile))
		{
			return true;
		}
	}
	if (!std::filesystem::exists(rIntermediatesDir / kpcPatchedArchetypeFile))
	{
		return true;
	}

	std::filesystem::path versionFile = rIntermediatesDir / kpcBakeVersionFile;
	if (!std::filesystem::exists(versionFile))
	{
		return true;
	}
	int32_t iFileVersion = 0;
	{
		std::ifstream versionStream(versionFile);
		versionStream >> iFileVersion;
	}
	if (iFileVersion != kiBakeVersion)
	{
		return true;
	}

	std::filesystem::file_time_type inputNewest = std::max(std::filesystem::last_write_time(rIslandJsonFile), std::filesystem::last_write_time(rArchetypeFile));
	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (std::filesystem::last_write_time(rIntermediatesDir / pcFile) < inputNewest)
		{
			return true;
		}
	}

	return false;
}

// True if any chunk leaf's derived split outputs are missing or stale — forces a re-split from the
// (assumed fresh) raw Gaea output, NOT a Gaea re-export. Checks the split-version sentinel (catches
// a kRouteSubdivisions columns/rows or ProcessBakedRegion change) and every leaf's per-region files.
bool AreLeavesDirty(const std::filesystem::path& rRouteDir, int64_t iLeafCount)
{
	std::filesystem::path splitVersionFile = rRouteDir / kpcIslandIntermediatesDir / kpcSplitVersionFile;
	if (!std::filesystem::exists(splitVersionFile))
	{
		return true;
	}
	int32_t iFileVersion = 0;
	{
		std::ifstream versionStream(splitVersionFile);
		versionStream >> iFileVersion;
	}
	if (iFileVersion != kiSplitVersion)
	{
		return true;
	}

	for (int64_t iLeaf = 0; iLeaf < iLeafCount; ++iLeaf)
	{
		// An absent leaf folder is an intentionally-rejected (too-low) leaf, not a dirty one -- skip it.
		// ProcessBakedRegion deletes rejected leaves, and the SplitVersion sentinel (checked above,
		// stamped last) keeps a crash mid-split from looking clean. Existing folders must be complete.
		std::filesystem::path leafDir = rRouteDir / std::to_string(iLeaf);
		if (!std::filesystem::exists(leafDir))
		{
			continue;
		}
		std::filesystem::path leafIntermediatesDir = leafDir / kpcIslandIntermediatesDir;
		if (!std::filesystem::exists(leafIntermediatesDir / kpcBakedDimensionsFile)
			|| !std::filesystem::exists(leafIntermediatesDir / "MeshProcessed.bin")
			|| !std::filesystem::exists(leafIntermediatesDir / "Elevation.r32")
			|| !std::filesystem::exists(leafIntermediatesDir / "AmbientOcclusion.r16"))
		{
			return true;
		}
	}

	return false;
}

} // namespace

// Bakes one route of one island in two stages. STAGE 1 (Gaea raw, slow — only when IsGaeaRawDirty):
// patch the route's Route Choice into a per-route archetype copy and run Gaea once at full
// texturePixels into the route's Intermediates/. STAGE 2 (split, fast — when IsGaeaRawDirty OR
// AreLeavesDirty): split the raw bake into UP TO iColumns × iRows chunk leaves via ProcessBakedRegion
// (up to 1 for 1x1, 2 for 2x1, 4 for 2x2 — chunks peaking below kfMinIslandMaxHeightMeters are rejected
// and produce no leaf, so indices can be sparse). A split-only change re-runs Stage 2 against the
// existing Stage-1 output — no Gaea re-export. Returns early when both stages are clean.
void BakeRoute(const IslandBakeContext& rContext, const RouteSubdivision& rRoute)
{
	const std::filesystem::path& rGaeaExecutable = rContext.rGaeaExecutable;
	const std::filesystem::path& rIslandFolder = rContext.rIslandFolder;
	const std::filesystem::path& rArchetypeFile = rContext.rArchetypeFile;
	const std::filesystem::path& rIslandJsonFile = rContext.rIslandJsonFile;
	const nlohmann::json& rIslandJson = rContext.rIslandJson;
	const WorldDimensions& rDimensions = rContext.rDimensions;
	const int32_t iSeed = rContext.iSeed;
	const int64_t iTexturePixels = rContext.iTexturePixels;
	const std::optional<int64_t>& oiMeshResolution = rContext.oiMeshResolution;

	std::filesystem::path routeDir = rIslandFolder / rRoute.pcLabel;
	std::filesystem::path intermediatesDir = routeDir / kpcIslandIntermediatesDir;
	int64_t iLeafCount = rRoute.iColumns * rRoute.iRows;

	bool bGaeaDirty = IsGaeaRawDirty(intermediatesDir, rIslandJsonFile, rArchetypeFile);
	bool bLeavesDirty = AreLeavesDirty(routeDir, iLeafCount);
	if (!bGaeaDirty && !bLeavesDirty)
	{
		return;
	}

	std::filesystem::create_directories(intermediatesDir);
	std::filesystem::path patchedArchetypeFile = intermediatesDir / kpcPatchedArchetypeFile;

	// STAGE 1 — Gaea raw export (slow). Skipped when the raw outputs are already present and fresh,
	// so a split-only change re-splits the existing bake without re-running Gaea.
	if (bGaeaDirty)
	{
		LOG(kDefault, kDebug, "Baking island route \"{}\" (Gaea export; archetype: \"{}\", seed: {}, texturePixels: {}, Route Choice: {})", routeDir.string(), rArchetypeFile.string(), iSeed, iTexturePixels, rRoute.iGaeaChoice);

		// Strip DataPacker-owned keys; remaining keys become Gaea graph variables. routes /
		// widthMeters / elevationMeters / seed / texturePixels are DataPacker-consumed: Gaea's
		// --vars can't reach Terrain.{Width,Height}, the Route Choice, or per-node Seed fields.
		nlohmann::json varsJson = rIslandJson;
		for (const char* pcKey : kpcRequiredIslandJsonKeys)
		{
			varsJson.erase(pcKey);
		}
		varsJson.erase("meshResolution");  // DataPacker-consumed; patched into the Mesher node directly.

		// Gaea.Swarm.exe trips on `--vars` pointing to an empty JSON object ("{}") with an opaque
		// "System.IO.IOException: The handle is invalid" during variable load. Only emit the vars
		// file and pass --vars when there are user variables left. Per-route temp name so concurrent
		// routes / islands don't collide.
		bool bHasVars = !varsJson.empty();
		std::filesystem::path varsFile = gpFileManager->mTempDirectory / std::format("{}-{}.gaea-vars.json", rIslandFolder.filename().string(), rRoute.pcLabel);
		common::ScopedLambda varsFileCleanup([&varsFile, bHasVars]()
		{
			if (bHasVars)
			{
				std::filesystem::remove(varsFile);
			}
		});
		if (bHasVars)
		{
			std::ofstream varsStream(varsFile);
			varsStream << varsJson.dump();
			varsStream.close();
		}

		// Copy the source archetype into this route's Intermediates/ and patch the copy — the on-disk
		// source .terrain is never mutated. PatchArchetype sets this route's Route Choice along with
		// dims / seeds / Mesher resolution. PatchedArchetype.terrain doubles as a debug artifact (open
		// in Gaea to inspect exactly what was baked, including the patched Route Choice).
		std::filesystem::copy_file(rArchetypeFile, patchedArchetypeFile, std::filesystem::copy_options::overwrite_existing);
		PatchArchetype(patchedArchetypeFile, rDimensions, iSeed, oiMeshResolution, rRoute.iGaeaChoice);

		// Gaea.Swarm.exe requires a real console for stdin/stdout/stderr — invoke via the new-console
		// helper. argv[0] is the executable's own path. --seed is dropped (per-node seeds were patched
		// into the archetype). Gaea bakes the patched copy in this route's Intermediates/.
		std::wstring commandLine;
		commandLine += L"\"" + rGaeaExecutable.native() + L"\"";
		commandLine += L" --silent";
		commandLine += L" --Filename \"" + patchedArchetypeFile.native() + L"\"";
		commandLine += L" --buildpath \"" + intermediatesDir.native() + L"\"";
		commandLine += std::format(L" --resolution {}", iTexturePixels);
		if (bHasVars)
		{
			commandLine += L" --vars \"" + varsFile.native() + L"\"";
		}

		LOG(kDefault, kDebug, "Running: \"{}\" --silent --Filename \"{}\" --buildpath \"{}\" --resolution {}{}{}", rGaeaExecutable.string(), patchedArchetypeFile.string(), intermediatesDir.string(), iTexturePixels, bHasVars ? " --vars " : "", bHasVars ? varsFile.string() : "");

		common::ExecutableResult result = common::RunExecutableInNewConsole(rGaeaExecutable, commandLine);

		if (result.miExitCode != 0)
		{
			throw std::runtime_error(std::format("Gaea.Swarm.exe exited with code {} for \"{}\". Use /gaea2-diagnose to examine the log file for failures.", result.miExitCode, routeDir.string()));
		}

		for (const char* pcFile : kpcIntermediateFiles)
		{
			if (!std::filesystem::exists(intermediatesDir / pcFile))
			{
				throw std::runtime_error(std::format("Gaea bake for \"{}\" did not produce \"{}\". Verify the archetype graph has an Export node named \"{}\" writing to Build Folder, and that the Elevation Export node uses FloatRaw32, AmbientOcclusion uses UshortRaw16, Color uses PNG8, and Normals uses Exr.", routeDir.string(), pcFile, std::filesystem::path(pcFile).stem().string()));
			}
		}

		// Stamp the Gaea-raw sentinel now the raw outputs are verified, so a later split-only re-run
		// (stale leaves / kiSplitVersion bump) reuses them without re-running Gaea.
		std::ofstream gaeaVersionStream(intermediatesDir / kpcBakeVersionFile);
		gaeaVersionStream << kiBakeVersion;
	}
	else
	{
		LOG(kDefault, kDebug, "Reusing Gaea bake for route \"{}\"; re-splitting only (no Gaea export)", routeDir.string());
	}

	// STAGE 2 — split (fast). Raw outputs are present here (just baked, or reused fresh).
	// PatchArchetype doesn't touch the Sea node, so the patched copy keeps the authored Level (or
	// kfGaeaSeaLevelDefault); the per-island beach offset drives the elevation transform, auto-crop
	// cut line, and mesh-Z scrub.
	float fSeaLevelNormalized = ReadArchetypeSeaLevel(patchedArchetypeFile);
	float fBeachOffsetMeters = fSeaLevelNormalized * rDimensions.fElevationMeters;
	LOG(kDefault, kDebug, "Archetype Sea Level (read, not patched): {} → beach offset {} m for elevationMeters {} m", common::Wb(fSeaLevelNormalized, 4), common::Wb(fBeachOffsetMeters, 2), common::Wb(rDimensions.fElevationMeters, 2));

	// Auto-crop cut line = -fBeachOffsetMeters + kfCropEpsilonAboveSeaFloorMeters. If the beach
	// offset is at or below the epsilon, the cut line lands at or above sea level and silently
	// strips the entire shoreline halo. Catch the authoring mistake here rather than shipping a
	// halo-less island.
	if (fBeachOffsetMeters <= kfCropEpsilonAboveSeaFloorMeters)
	{
		throw std::runtime_error(std::format("Island \"{}\" beach offset ({:.2f} m, = Sea Level {:.4f} × elevationMeters {:.2f} m) is at or below the crop epsilon ({:.2f} m): the auto-crop would land at or above sea level and strip the shoreline halo. Raise elevationMeters, raise the archetype Sea node's Level, or lower kfCropEpsilonAboveSeaFloorMeters.", rIslandFolder.string(), fBeachOffsetMeters, fSeaLevelNormalized, rDimensions.fElevationMeters, kfCropEpsilonAboveSeaFloorMeters));
	}

	// Per-island sea floor must match the engine-wide kfSeaBottomMeters so the elevation RTT clear
	// (Engine/Source/Graphics/Managers/RenderTargetTextures.cpp) blends seamlessly with edge texels.
	ASSERT(std::abs(-fBeachOffsetMeters - common::kfSeaBottomMeters) < 0.01f);

	// Read raw elevation and convert to engine-meters: pixel_m = (pixel_normalized -
	// fSeaLevelNormalized) × elevationMeters. Beach = 0, ocean negative (to -fBeachOffsetMeters),
	// land positive. NaN/Inf scrubbed to the sea floor (R32_SFLOAT is unbounded; a stray non-finite
	// pixel would poison the elevation G-buffer and vertex displacement). The full-res buffer feeds
	// every chunk's ProcessBakedRegion crop; the raw Elevation.r32 stays on disk as the bake source.
	std::filesystem::path elevationFile = intermediatesDir / "Elevation.r32";
	std::vector<float> fullElevationMeters(static_cast<size_t>(iTexturePixels) * static_cast<size_t>(iTexturePixels));
	int64_t iExpectedBytes = static_cast<int64_t>(fullElevationMeters.size() * sizeof(float));
	int64_t iActualBytes = static_cast<int64_t>(std::filesystem::file_size(elevationFile));
	if (iActualBytes != iExpectedBytes)
	{
		throw std::runtime_error(std::format("Gaea produced \"{}\" at {} bytes; expected {} bytes ({}x{} float32). Verify the archetype's Elevation Export node uses FloatRaw32 format and is unconstrained by an internal resolution override.", elevationFile.string(), iActualBytes, iExpectedBytes, iTexturePixels, iTexturePixels));
	}
	{
		std::ifstream readStream(elevationFile, std::ios::binary);
		readStream.read(reinterpret_cast<char*>(fullElevationMeters.data()), iExpectedBytes);
	}
	for (float& rfPixel : fullElevationMeters)
	{
		rfPixel = std::isfinite(rfPixel) ? (rfPixel - fSeaLevelNormalized) * rDimensions.fElevationMeters : -fBeachOffsetMeters;
	}

	// Read raw full-resolution AmbientOcclusion (cropped per chunk in ProcessBakedRegion).
	std::filesystem::path ambientOcclusionFile = intermediatesDir / "AmbientOcclusion.r16";
	std::vector<uint16_t> fullAmbientOcclusion(static_cast<size_t>(iTexturePixels) * static_cast<size_t>(iTexturePixels));
	{
		int64_t iExpectedAoBytes = iTexturePixels * iTexturePixels * static_cast<int64_t>(sizeof(uint16_t));
		int64_t iActualAoBytes = static_cast<int64_t>(std::filesystem::file_size(ambientOcclusionFile));
		if (iActualAoBytes != iExpectedAoBytes)
		{
			throw std::runtime_error(std::format("Gaea produced \"{}\" at {} bytes; expected {} bytes ({}x{} uint16). Verify the archetype's AmbientOcclusion Export node uses UshortRaw16 format.", ambientOcclusionFile.string(), iActualAoBytes, iExpectedAoBytes, iTexturePixels, iTexturePixels));
		}
		std::ifstream readStream(ambientOcclusionFile, std::ios::binary);
		readStream.read(reinterpret_cast<char*>(fullAmbientOcclusion.data()), iExpectedAoBytes);
	}

	// Parse Mesh.gltf (Mesher's glTF separate-format manifest; references Mesh.bin) into a flat
	// float3-positions / uint32-indices mesh in island-local meters, then run the adaptive
	// beach-band subdivision ONCE on the full mesh (the per-chunk region crops reuse the densified
	// result). glTF 2.0 mandates right-handed Y-up: Mesher emits (X_east, Y_height, Z_south); the
	// mapping (X,Y,Z)=(gltf.x, -gltf.z, gltf.y) has determinant +1 so handedness / CCW winding are
	// preserved, engine Y increases northward, engine Z is up. Height gets the per-island beach
	// offset subtracted so sea level = 0. UVs (TEXCOORD_0) are discarded — runtime derives
	// visible-area UV from world XY. Indices are upcast to uint32. The actual chunk crop / re-center
	// / write happens later in ProcessBakedRegion.
	std::vector<float> meshPositions;
	std::vector<uint32_t> meshIndices;
	{
		std::filesystem::path meshGltfFile = intermediatesDir / "Mesh.gltf";
		tinygltf::Model gltfModel;
		std::string error;
		std::string warning;
		tinygltf::TinyGLTF gltfContext;
		bool bLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, meshGltfFile.string());
		if (!bLoaded)
		{
			throw std::runtime_error(std::format("Failed to parse Gaea Mesher output \"{}\": {} (warning: {}). Verify the archetype has a Mesher node with Format=GLTF and that Gaea wrote both Mesh.gltf and Mesh.bin.", meshGltfFile.string(), error, warning));
		}
		if (gltfModel.meshes.empty() || gltfModel.meshes[0].primitives.empty())
		{
			throw std::runtime_error(std::format("Gaea Mesher output \"{}\" contains no mesh primitives.", meshGltfFile.string()));
		}

		const tinygltf::Primitive& rPrimitive = gltfModel.meshes[0].primitives[0];
		auto positionIt = rPrimitive.attributes.find("POSITION");
		if (positionIt == rPrimitive.attributes.end() || rPrimitive.indices < 0)
		{
			throw std::runtime_error(std::format("Gaea Mesher output \"{}\" primitive missing POSITION attribute or indices accessor.", meshGltfFile.string()));
		}

		const tinygltf::Accessor& rPosAccessor = gltfModel.accessors.at(static_cast<size_t>(positionIt->second));
		const tinygltf::BufferView& rPosView = gltfModel.bufferViews.at(static_cast<size_t>(rPosAccessor.bufferView));
		const tinygltf::Buffer& rPosBuffer = gltfModel.buffers.at(static_cast<size_t>(rPosView.buffer));
		if (rPosAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || rPosAccessor.type != TINYGLTF_TYPE_VEC3)
		{
			throw std::runtime_error(std::format("Gaea Mesher output \"{}\" POSITION accessor is not float3.", meshGltfFile.string()));
		}

		int64_t iVertexCount = static_cast<int64_t>(rPosAccessor.count);
		meshPositions.resize(static_cast<size_t>(iVertexCount) * 3);
		{
			const std::byte* pSrc = reinterpret_cast<const std::byte*>(rPosBuffer.data.data()) + rPosView.byteOffset + rPosAccessor.byteOffset;
			size_t uiStride = rPosAccessor.ByteStride(rPosView);
			for (int64_t iVertex = 0; iVertex < iVertexCount; ++iVertex)
			{
				const float* pfXyz = reinterpret_cast<const float*>(pSrc + static_cast<size_t>(iVertex) * uiStride);
				float fX = pfXyz[0];
				float fY = -pfXyz[2];
				float fZ = std::isfinite(pfXyz[1]) ? pfXyz[1] - fBeachOffsetMeters : -fBeachOffsetMeters;
				meshPositions[static_cast<size_t>(iVertex) * 3 + 0] = fX;
				meshPositions[static_cast<size_t>(iVertex) * 3 + 1] = fY;
				meshPositions[static_cast<size_t>(iVertex) * 3 + 2] = fZ;
			}
		}

		const tinygltf::Accessor& rIdxAccessor = gltfModel.accessors.at(static_cast<size_t>(rPrimitive.indices));
		const tinygltf::BufferView& rIdxView = gltfModel.bufferViews.at(static_cast<size_t>(rIdxAccessor.bufferView));
		const tinygltf::Buffer& rIdxBuffer = gltfModel.buffers.at(static_cast<size_t>(rIdxView.buffer));
		int64_t iIndexCount = static_cast<int64_t>(rIdxAccessor.count);
		meshIndices.resize(static_cast<size_t>(iIndexCount));
		{
			const std::byte* pSrc = reinterpret_cast<const std::byte*>(rIdxBuffer.data.data()) + rIdxView.byteOffset + rIdxAccessor.byteOffset;
			switch (rIdxAccessor.componentType)
			{
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					std::memcpy(meshIndices.data(), pSrc, static_cast<size_t>(iIndexCount) * sizeof(uint32_t));
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				{
					const uint16_t* puiSrc = reinterpret_cast<const uint16_t*>(pSrc);
					for (int64_t i = 0; i < iIndexCount; ++i)
					{
						meshIndices[static_cast<size_t>(i)] = puiSrc[i];
					}
					break;
				}
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
				{
					const uint8_t* puiSrc = reinterpret_cast<const uint8_t*>(pSrc);
					for (int64_t i = 0; i < iIndexCount; ++i)
					{
						meshIndices[static_cast<size_t>(i)] = puiSrc[i];
					}
					break;
				}
				default:
					throw std::runtime_error(std::format("Gaea Mesher output \"{}\" indices accessor has unsupported componentType {}.", meshGltfFile.string(), rIdxAccessor.componentType));
			}
		}

		int64_t iInitialVertexCount = iVertexCount;
		int64_t iInitialTriangleCount = iIndexCount / 3;

		// Adaptive beach-band subdivision. Densifies triangles whose Z-range overlaps the band so
		// the shore silhouette and shore-material blend get enough vertex resolution. In-band tris
		// do 1->4 midpoint splits until longest XY edge <= kfBeachSubdivisionMaxEdgeMeters. Out-of-band
		// neighbors that inherit a midpoint via a shared edge do the MINIMAL absorption split
		// (1->2 for one midpoint, 1->3 for two, 1->4 for all three) -- introducing no new midpoints,
		// so the cascade dies at one ring. Linear-interp Z for new midpoints is fine because
		// Terrain.vert overrides Z from the heightmap at rasterization; mesh Z exists only for the
		// in-band test, and an in-band split's children inherit Z values that are subsets of the
		// parent's range. Runs once on the full mesh; chunk crops below reuse the densified mesh.
		SubdivisionConfig subdivisionConfig
		{
			.fBandMinMeters = kfBeachSubdivisionMinMeters,
			.fBandMaxMeters = kfBeachSubdivisionMaxMeters,
			.fMaxEdgeMeters = kfBeachSubdivisionMaxEdgeMeters,
			.iMaxDepth = kiBeachSubdivisionMaxDepth,
		};
		int64_t iDepthCapHits = 0;
		SubdivideBeachBand(meshPositions, meshIndices, subdivisionConfig, iDepthCapHits);
		iVertexCount = static_cast<int64_t>(meshPositions.size() / 3);
		iIndexCount = static_cast<int64_t>(meshIndices.size());

		if (iDepthCapHits > 0)
		{
			LOG(kDefault, kWarning, "Mesh \"{}\": beach subdivision hit depth cap ({}) on {} triangle(s); largest input triangles may still exceed {:.2f}m edge target AND the mesh may contain T-junction cracks where capped absorption-needing triangles were skipped (raise kiBeachSubdivisionMaxDepth or split it into separate in-band / absorption caps if observed)", routeDir.string(), kiBeachSubdivisionMaxDepth, iDepthCapHits, kfBeachSubdivisionMaxEdgeMeters);
		}
		LOG(kDefault, kDebug, "Mesh \"{}\": {} -> {} vertices, {} -> {} triangles after beach subdivision (band Z=[{:.2f}, {:.2f}]m, edge target {:.2f}m)", routeDir.string(), iInitialVertexCount, iVertexCount, iInitialTriangleCount, iIndexCount / 3, subdivisionConfig.fBandMinMeters, subdivisionConfig.fBandMaxMeters, kfBeachSubdivisionMaxEdgeMeters);
	}

	// Split into chunks (up to 1 for 1x1, iColumns × iRows otherwise) and write each leaf that clears
	// the minimum-height threshold (ProcessBakedRegion rejects too-low / underwater chunks). The X / Y
	// region boundaries partition the full texture (uneven when columns/rows don't divide it evenly);
	// ProcessBakedRegion auto-crops within each, borrowing neighbour pixels across a seam for alignment.
	// textureSourceDir is leaf-relative ("../Intermediates") so ExportIsland reads the shared
	// full-res Color / Normals / mask sources from this route's Intermediates dir.
	std::string textureSourceDirRelative = std::format("../{}", kpcIslandIntermediatesDir);
	BakeOutput bakeOutput {.rFullElevationMeters = fullElevationMeters, .rFullAmbientOcclusion = fullAmbientOcclusion, .fBeachOffsetMeters = fBeachOffsetMeters};
	int64_t iWrittenLeaves = 0;
	for (int64_t iColumn = 0; iColumn < rRoute.iColumns; ++iColumn)
	{
		for (int64_t iRow = 0; iRow < rRoute.iRows; ++iRow)
		{
			int64_t iChunkIndex = iColumn * rRoute.iRows + iRow;
			RegionBounds region
			{
				.iStartX = iColumn * iTexturePixels / rRoute.iColumns,
				.iEndX = (iColumn + 1) * iTexturePixels / rRoute.iColumns,
				.iStartY = iRow * iTexturePixels / rRoute.iRows,
				.iEndY = (iRow + 1) * iTexturePixels / rRoute.iRows,
			};
			std::filesystem::path leafDir = routeDir / std::to_string(iChunkIndex);
			LeafTarget leaf {.rLeafDir = leafDir, .rTextureSourceDirRelative = textureSourceDirRelative};
			if (ProcessBakedRegion(rContext, bakeOutput, region, meshPositions, meshIndices, leaf))
			{
				++iWrittenLeaves;
			}
		}
	}

	if (iWrittenLeaves == 0)
	{
		throw std::runtime_error(std::format("Island route \"{}\": every one of {} chunk(s) was rejected as too low (peak < {:.2f} m). Raise Island.json's elevationMeters, lower kfMinIslandMaxHeightMeters, or remove this route from Island.json.", routeDir.string(), iLeafCount, kfMinIslandMaxHeightMeters));
	}

	// Stamp the split sentinel last, after every leaf is written. A crash mid-split leaves it absent
	// (or stale), so AreLeavesDirty re-splits next run — without re-running Gaea (BakeVersion is
	// already stamped above, so IsGaeaRawDirty stays clean).
	{
		std::ofstream splitVersionStream(intermediatesDir / kpcSplitVersionFile);
		splitVersionStream << kiSplitVersion;
	}

	LOG(kDefault, kDebug, "Island route \"{}\" ready ({} of {} chunk(s) written, {} rejected as too low{})", routeDir.string(), iWrittenLeaves, iLeafCount, iLeafCount - iWrittenLeaves, bGaeaDirty ? ", Gaea re-baked" : ", split-only reuse");
}
