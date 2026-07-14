#include "BakeIslandIntermediatesInternal.h"

#include "FileManager.h"
#include "GaeaArchetype.h"
#include "SubdivideBeachBand.h"

namespace
{

// One shared cache folder per route under FileManager::mGaeaCacheDirectory.
// One Gaea bake per route produces all files below at texturePixels resolution; the raw
// Elevation.r32 stays as the bake source (it is NOT rewritten in place) — ProcessBakedRegion reads
// it and writes the per-chunk downsampled (texturePixels / kiElevationDivisor) elevation into each
// cache leaf. Elevation.r32 is headerless IEEE-754 float (Gaea's FloatRaw32 format),
// normalized [0,1] at bake time — DataPacker reads the Sea node Level from the archetype (fallback
// kfGaeaSeaLevelDefault when absent), then scales to meters by elevationMeters and subtracts
// `Level × elevationMeters` so beach = 0 in engine space and the sea floor sits at
// -(Level × elevationMeters) per island. AmbientOcclusion.r16 stays UshortRaw16 (precision-matched
// to BC4_UNORM). Color is 8-bit PNG (sRGB-encoded display data; downstream BC7 is 8-bit anyway, so
// EXR's float precision was wasted and its color-space convention conflicts with Gaea writing
// sRGB-encoded values into the EXR container). Normals stay multi-channel EXR. Files written by
// Gaea directly. Used for both the post-Gaea existence verification and the IsGaeaRawDirty
// existence check. These raw outputs live in each route's shared Gaea cache folder.
constexpr const char* kpcIntermediateFiles[] =
{
	"AmbientOcclusion.r16",
	"Color.png",
	"Elevation.r32",
	"Normals.exr",
	"Flow.png",
	"Rock.png",
	"Sand.png",
	"Snow.png",
	"Mesh.gltf",  // Gaea Mesher output: glTF JSON manifest (separate-format)
	"Mesh.bin",   // Gaea Mesher output: binary buffer referenced by Mesh.gltf
};

// MeshProcessed.bin (positions + indices in island-local meters, XY-centered, sea-level Z=0) is
// derived per chunk leaf, not in the route's raw cache root — the per-leaf dirty check in
// AreLeavesDirty verifies it (and Elevation.r32 / AmbientOcclusion.r16 / BakedDimensions.json)
// exists in each chunk folder.

// Two-stage fingerprint metadata files, both route-level in the Gaea cache. The bake separates the
// SLOW Gaea raw export from the FAST post-Gaea split so a split-only change (a kRouteSubdivisions
// columns/rows edit, or ProcessBakedRegion crop logic) re-splits from the existing Gaea output
// WITHOUT re-running Gaea.Swarm.
//
// kiBakeVersion (BakeVersion.meta): the Gaea RAW output. Bump only when something that changes the
// raw bake changes — the archetype patch (dims / seed / Route Choice / Mesher resolution) or the
// Gaea invocation. IsGaeaRawDirty re-runs Gaea on mismatch.
//
// kiSplitVersion (SplitVersion.meta): the post-Gaea split. Bump when ProcessBakedRegion or the chunk
// split (incl. kRouteSubdivisions columns/rows) changes. AreLeavesDirty re-splits from the existing
// raw on mismatch — no Gaea re-export.
constexpr int32_t kiBakeVersion = 28;
constexpr int32_t kiSplitVersion = 5;

// Beach-band adaptive subdivision constants. After the Gaea Mesher mesh is parsed, every triangle
// whose Z-range overlaps the beach band gets recursively split (1->4 midpoint) until its longest XY
// edge falls below the target. Out-of-band neighbors that inherit a midpoint via a shared edge get
// the minimal absorption split (1->2 for 1 midpoint, 1->3 for 2, true 1->4 for 3) -- introducing no
// new midpoints, so the cascade dies at one ring around the band.
// Band straddles beach (engine-Z = 0): triangles whose Z-range overlaps
// [kfBeachSubdivisionMinMeters, kfBeachSubdivisionMaxMeters] in absolute engine-meters densify,
// so both shallow water and just-above-beach terrain are covered. Independent of elevationMeters;
// set the two bounds asymmetrically to widen the underwater or above-water side independently.
constexpr float kfBeachSubdivisionMinMeters = -0.25f;
constexpr float kfBeachSubdivisionMaxMeters = 0.5f;
constexpr float kfBeachSubdivisionMaxEdgeMeters = 1.0f;
constexpr int32_t kiBeachSubdivisionMaxDepth = 12;

constexpr const char* kpcBakeVersionFile = "BakeVersion.meta";
constexpr const char* kpcSplitVersionFile = "SplitVersion.meta";
constexpr const char* kpcPatchedArchetypeFile = "PatchedArchetype.terrain";
constexpr const char* kpcGaeaStagingDirectory = "GaeaStaging";

std::string ReadTextFile(const std::filesystem::path& rFile)
{
	std::ifstream stream(rFile, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void WriteTextFile(const std::filesystem::path& rFile, const std::string& rText)
{
	std::ofstream stream(rFile, std::ios::binary | std::ios::trunc);
	stream.write(rText.data(), static_cast<std::streamsize>(rText.size()));
	stream.close();
	VERIFY_SUCCESS(stream.good());
}

std::string BakeFingerprint(const IslandBakeContext& rContext, const RouteSubdivision& rRoute)
{
	nlohmann::json metadata;
	metadata["version"] = kiBakeVersion;
	metadata["island"] = gpFileManager->GetFingerprint(rContext.rIslandJsonFile);
	metadata["archetype"] = gpFileManager->GetFingerprint(rContext.rArchetypeFile);
	metadata["route"] = rRoute.pcLabel;
	metadata["choice"] = rRoute.iGaeaChoice;
	return metadata.dump();
}

std::string SplitFingerprint(const RouteSubdivision& rRoute)
{
	nlohmann::json metadata;
	metadata["version"] = kiSplitVersion;
	metadata["route"] = rRoute.pcLabel;
	metadata["columns"] = rRoute.iColumns;
	metadata["rows"] = rRoute.iRows;
	return metadata.dump();
}

// True if the route's RAW Gaea outputs are missing or stale — forces a (slow) Gaea.Swarm re-export.
// Checks only Gaea-output concerns: the raw intermediate files + the patched archetype (needed for
// the split's sea-level read) are present, the Gaea-bake-version sentinel matches, and the raw
// metadata matches the content fingerprints of Island.json / the archetype and the route identity.
// The post-Gaea split is checked separately
// by AreLeavesDirty so a split-only change never trips this.
bool IsGaeaRawDirty(const std::filesystem::path& rIntermediatesDirectory, const std::string& rExpectedFingerprint)
{
	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (!std::filesystem::exists(rIntermediatesDirectory / pcFile))
		{
			return true;
		}
	}
	if (!std::filesystem::exists(rIntermediatesDirectory / kpcPatchedArchetypeFile))
	{
		return true;
	}

	std::filesystem::path versionFile = rIntermediatesDirectory / kpcBakeVersionFile;
	if (!std::filesystem::exists(versionFile))
	{
		return true;
	}
	return ReadTextFile(versionFile) != rExpectedFingerprint;
}

// True if any chunk leaf's derived split outputs are missing or stale — forces a re-split from the
// (assumed fresh) raw Gaea output, NOT a Gaea re-export. Checks the split-version sentinel (catches
// a kRouteSubdivisions columns/rows or ProcessBakedRegion change) and every leaf's per-region files.
bool AreLeavesDirty(const std::filesystem::path& rRouteDirectory, const std::filesystem::path& rCacheRouteDirectory, int64_t iLeafCount, const std::string& rExpectedFingerprint)
{
	std::filesystem::path splitVersionFile = rCacheRouteDirectory / kpcSplitVersionFile;
	if (!std::filesystem::exists(splitVersionFile))
	{
		return true;
	}
	if (ReadTextFile(splitVersionFile) != rExpectedFingerprint)
	{
		return true;
	}

	for (int64_t iLeaf = 0; iLeaf < iLeafCount; ++iLeaf)
	{
		// An absent leaf folder is an intentionally-rejected (too-low) leaf, not a dirty one -- skip it.
		// ProcessBakedRegion deletes rejected leaves, and the SplitVersion sentinel (checked above,
		// stamped last) keeps a crash mid-split from looking clean. Existing folders must be complete.
		std::filesystem::path sourceLeafDirectory = rRouteDirectory / std::to_string(iLeaf);
		std::filesystem::path cacheLeafDirectory = rCacheRouteDirectory / std::to_string(iLeaf);
		if (!std::filesystem::exists(sourceLeafDirectory) && !std::filesystem::exists(cacheLeafDirectory))
		{
			continue;
		}
		if (!std::filesystem::exists(cacheLeafDirectory / kpcBakedDimensionsFile)
			|| !std::filesystem::exists(cacheLeafDirectory / "MeshProcessed.bin")
			|| !std::filesystem::exists(cacheLeafDirectory / "Elevation.r32")
			|| !std::filesystem::exists(cacheLeafDirectory / "AmbientOcclusion.r16"))
		{
			return true;
		}
		std::filesystem::create_directories(sourceLeafDirectory);
	}

	return false;
}

// Delete numeric leaf folders left over from a previous, larger split. The split loop and AreLeavesDirty
// only ever visit indices 0 .. iLeafCount-1, so when a route's kRouteSubdivisions columns/rows shrink the
// higher-index folders are never revisited: they persist carrying cached BakedDimensions.json and
// ExportIsland::Handles ingests them as stale kIsland chunks (a kiSplitVersion bump does not help — the
// re-split only rewrites the lower-count leaves). Mirror BakeOne's whole-route prune one level up: collect
// first, then remove_all (mutating the directory mid-iteration is unspecified for directory_iterator). Runs
// unconditionally before the dirty early-return, so the leaf-set invariant holds regardless of whether the
// developer bumped kiSplitVersion. Non-numeric route metadata is untouched; kept leaves
// (index < iLeafCount) are never removed.
void RemoveOrphanedLeafFolders(const std::filesystem::path& rRouteDir, int64_t iLeafCount)
{
	if (!std::filesystem::exists(rRouteDir))
	{
		return;
	}

	std::vector<std::filesystem::path> orphanedLeafFolders;
	for (const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(rRouteDir))
	{
		if (!rEntry.is_directory())
		{
			continue;
		}
		std::string name = rEntry.path().filename().string();
		if (name.empty() || !std::ranges::all_of(name, [](char cChar) { return cChar >= '0' && cChar <= '9'; }))
		{
			continue;
		}
		// std::stoll throws std::out_of_range on an all-digit name too long for int64_t (>= 19 digits). The
		// split loop only ever creates leaf folders for indices 0 .. iLeafCount-1, so a name with more digits
		// than int64_t always holds (digits10 == 18) can't be one of ours — skip it rather than parse (and
		// never remove_all a folder we can't confidently classify).
		if (name.size() > static_cast<size_t>(std::numeric_limits<int64_t>::digits10))
		{
			continue;
		}
		if (std::stoll(name) >= iLeafCount)
		{
			orphanedLeafFolders.push_back(rEntry.path());
		}
	}
	for (const std::filesystem::path& rOrphanedLeafFolder : orphanedLeafFolders)
	{
		std::filesystem::remove_all(rOrphanedLeafFolder);
		LOG(kDefault, kDebug, "Removed orphaned island leaf folder: \"{}\"", rOrphanedLeafFolder.string());
	}
}

// STAGE 1 — Gaea raw export (slow). Strips DataPacker-owned keys to vars, copies + patches the
// archetype, runs Gaea.Swarm once at full texturePixels into a staging directory, then verifies the
// staged raw outputs exist. The caller publishes them and stamps the sentinel. Called only when
// IsGaeaRawDirty.
void RunGaeaExport(const IslandBakeContext& rContext, const RouteSubdivision& rRoute, const std::filesystem::path& rRouteDirectory, const std::filesystem::path& rStagingDirectory, const std::filesystem::path& rPatchedArchetypeFile)
{
	LOG(kDefault, kDebug, "Baking island route \"{}\" (Gaea export; archetype: \"{}\", seed: {}, texturePixels: {}, Route Choice: {})", rRouteDirectory.string(), rContext.rArchetypeFile.string(), rContext.iSeed, rContext.iTexturePixels, rRoute.iGaeaChoice);

	// Strip DataPacker-owned keys; remaining keys become Gaea graph variables. routes /
	// widthMeters / elevationMeters / seed / texturePixels are DataPacker-consumed: Gaea's
	// --vars can't reach Terrain.{Width,Height}, the Route Choice, or per-node Seed fields.
	nlohmann::json varsJson = rContext.rIslandJson;
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
	std::filesystem::path varsFile = gpFileManager->mTempDirectory / std::format("{}-{}.gaea-vars.json", rContext.rIslandFolder.filename().string(), rRoute.pcLabel);
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
		VERIFY_SUCCESS(varsStream.good());
	}

	// Copy the source archetype into this route's cache and patch the copy — the on-disk
	// source .terrain is never mutated. PatchArchetype sets this route's Route Choice along with
	// dims / seeds / Mesher resolution. PatchedArchetype.terrain doubles as a debug artifact (open
	// in Gaea to inspect exactly what was baked, including the patched Route Choice).
	std::filesystem::copy_file(rContext.rArchetypeFile, rPatchedArchetypeFile, std::filesystem::copy_options::overwrite_existing);
	PatchArchetype(rPatchedArchetypeFile, rContext.rDimensions, rContext.iSeed, rContext.oiMeshResolution, rRoute.iGaeaChoice);

	// Gaea.Swarm.exe requires a real console for stdin/stdout/stderr — invoke via the new-console
	// helper. argv[0] is the executable's own path. --seed is dropped (per-node seeds were patched
	// into the archetype). Gaea bakes the patched copy in this route's staging directory.
	std::wstring commandLine;
	commandLine += L"\"" + rContext.rGaeaExecutable.native() + L"\"";
	commandLine += L" --silent";
	commandLine += L" --Filename \"" + rPatchedArchetypeFile.native() + L"\"";
	commandLine += L" --buildpath \"" + rStagingDirectory.native() + L"\"";
	commandLine += std::format(L" --resolution {}", rContext.iTexturePixels);
	if (bHasVars)
	{
		commandLine += L" --vars \"" + varsFile.native() + L"\"";
	}

	if (gpFileManager->mbForbidExpensiveExport)
	{
		throw std::runtime_error("Gaea.Swarm export blocked by BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT=1");
	}

	LOG(kDefault, kDebug, "Running: {}", commandLine);
	common::ExecutableResult result = common::RunExecutableInNewConsole(rContext.rGaeaExecutable, commandLine);

	if (result.miExitCode != 0)
	{
		throw std::runtime_error(std::format("Gaea.Swarm.exe exited with code {} for \"{}\". Use /gaea2-diagnose to examine the log file for failures.", result.miExitCode, rRouteDirectory.string()));
	}

	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (!std::filesystem::exists(rStagingDirectory / pcFile))
		{
			throw std::runtime_error(std::format("Gaea bake for \"{}\" did not produce \"{}\". Verify the archetype graph has an Export node named \"{}\" writing to Build Folder, and that the Elevation Export node uses FloatRaw32, AmbientOcclusion uses UshortRaw16, Color uses PNG8, and Normals uses Exr.", rRouteDirectory.string(), pcFile, std::filesystem::path(pcFile).stem().string()));
		}
	}

}

// Read raw elevation and convert to engine-meters: pixel_m = (pixel_normalized -
// fSeaLevelNormalized) × elevationMeters. Beach = 0, ocean negative (to -fBeachOffsetMeters),
// land positive. NaN/Inf scrubbed to the sea floor (R32_SFLOAT is unbounded; a stray non-finite
// pixel would poison the elevation G-buffer and vertex displacement). The full-res buffer feeds
// every chunk's ProcessBakedRegion crop; the raw Elevation.r32 stays on disk as the bake source.
std::vector<float> LoadElevationMeters(const std::filesystem::path& rIntermediatesDir, int64_t iTexturePixels, float fSeaLevelNormalized, float fElevationMeters, float fBeachOffsetMeters)
{
	std::filesystem::path elevationFile = rIntermediatesDir / "Elevation.r32";
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
		rfPixel = std::isfinite(rfPixel) ? (rfPixel - fSeaLevelNormalized) * fElevationMeters : -fBeachOffsetMeters;
	}
	return fullElevationMeters;
}

// Read raw full-resolution AmbientOcclusion (cropped per chunk in ProcessBakedRegion).
std::vector<uint16_t> LoadAmbientOcclusion(const std::filesystem::path& rIntermediatesDir, int64_t iTexturePixels)
{
	std::filesystem::path ambientOcclusionFile = rIntermediatesDir / "AmbientOcclusion.r16";
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
	return fullAmbientOcclusion;
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
void LoadMesherMesh(const std::filesystem::path& rIntermediatesDir, float fBeachOffsetMeters, const std::filesystem::path& rRouteDir, std::vector<float>& rMeshPositions, std::vector<uint32_t>& rMeshIndices)
{
	std::filesystem::path meshGltfFile = rIntermediatesDir / "Mesh.gltf";
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
	rMeshPositions.resize(static_cast<size_t>(iVertexCount) * 3);
	{
		const std::byte* pSrc = reinterpret_cast<const std::byte*>(rPosBuffer.data.data()) + rPosView.byteOffset + rPosAccessor.byteOffset;
		size_t uiStride = rPosAccessor.ByteStride(rPosView);
		for (int64_t iVertex = 0; iVertex < iVertexCount; ++iVertex)
		{
			const float* pfXyz = reinterpret_cast<const float*>(pSrc + static_cast<size_t>(iVertex) * uiStride);
			float fX = pfXyz[0];
			float fY = -pfXyz[2];
			float fZ = std::isfinite(pfXyz[1]) ? pfXyz[1] - fBeachOffsetMeters : -fBeachOffsetMeters;
			rMeshPositions[static_cast<size_t>(iVertex) * 3 + 0] = fX;
			rMeshPositions[static_cast<size_t>(iVertex) * 3 + 1] = fY;
			rMeshPositions[static_cast<size_t>(iVertex) * 3 + 2] = fZ;
		}
	}

	const tinygltf::Accessor& rIdxAccessor = gltfModel.accessors.at(static_cast<size_t>(rPrimitive.indices));
	const tinygltf::BufferView& rIdxView = gltfModel.bufferViews.at(static_cast<size_t>(rIdxAccessor.bufferView));
	const tinygltf::Buffer& rIdxBuffer = gltfModel.buffers.at(static_cast<size_t>(rIdxView.buffer));
	int64_t iIndexCount = static_cast<int64_t>(rIdxAccessor.count);
	rMeshIndices.resize(static_cast<size_t>(iIndexCount));
	{
		const std::byte* pSrc = reinterpret_cast<const std::byte*>(rIdxBuffer.data.data()) + rIdxView.byteOffset + rIdxAccessor.byteOffset;
		switch (rIdxAccessor.componentType)
		{
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				std::memcpy(rMeshIndices.data(), pSrc, static_cast<size_t>(iIndexCount) * sizeof(uint32_t));
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			{
				const uint16_t* puiSrc = reinterpret_cast<const uint16_t*>(pSrc);
				for (int64_t i = 0; i < iIndexCount; ++i)
				{
					rMeshIndices[static_cast<size_t>(i)] = puiSrc[i];
				}
				break;
			}
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			{
				const uint8_t* puiSrc = reinterpret_cast<const uint8_t*>(pSrc);
				for (int64_t i = 0; i < iIndexCount; ++i)
				{
					rMeshIndices[static_cast<size_t>(i)] = puiSrc[i];
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
	SubdivideBeachBand(rMeshPositions, rMeshIndices, subdivisionConfig, iDepthCapHits);
	iVertexCount = static_cast<int64_t>(rMeshPositions.size() / 3);
	iIndexCount = static_cast<int64_t>(rMeshIndices.size());

	if (iDepthCapHits > 0)
	{
		LOG(kDefault, kWarning, "Mesh \"{}\": beach subdivision hit depth cap ({}) on {} triangle(s); largest input triangles may still exceed {:.2f}m edge target AND the mesh may contain T-junction cracks where capped absorption-needing triangles were skipped (raise kiBeachSubdivisionMaxDepth or split it into separate in-band / absorption caps if observed)", rRouteDir.string(), kiBeachSubdivisionMaxDepth, iDepthCapHits, kfBeachSubdivisionMaxEdgeMeters);
	}
	LOG(kDefault, kDebug, "Mesh \"{}\": {} -> {} vertices, {} -> {} triangles after beach subdivision (band Z=[{:.2f}, {:.2f}]m, edge target {:.2f}m)", rRouteDir.string(), iInitialVertexCount, iVertexCount, iInitialTriangleCount, iIndexCount / 3, subdivisionConfig.fBandMinMeters, subdivisionConfig.fBandMaxMeters, kfBeachSubdivisionMaxEdgeMeters);
}

} // namespace

// Bakes one route of one island in two stages. STAGE 1 (Gaea raw, slow — only when IsGaeaRawDirty):
// patch the route's Route Choice into a per-route archetype copy and run Gaea once at full
// texturePixels into the route cache. STAGE 2 (split, fast — when IsGaeaRawDirty OR
// AreLeavesDirty): split the raw bake into UP TO iColumns × iRows chunk leaves via ProcessBakedRegion
// (up to 1 for 1x1, 2 for 2x1, 4 for 2x2 — chunks peaking below kfMinIslandMaxHeightMeters are rejected
// and produce no leaf, so indices can be sparse). A split-only change re-runs Stage 2 against the
// existing Stage-1 output — no Gaea re-export. Returns early when both stages are clean.
void BakeRoute(const IslandBakeContext& rContext, const RouteSubdivision& rRoute)
{
	std::filesystem::path routeDirectory = rContext.rIslandFolder / rRoute.pcLabel;
	std::filesystem::path intermediatesDirectory = rContext.rCacheIslandFolder / rRoute.pcLabel;
	int64_t iLeafCount = rRoute.iColumns * rRoute.iRows;
	std::string bakeFingerprint = BakeFingerprint(rContext, rRoute);
	std::string splitFingerprint = SplitFingerprint(rRoute);

	// Sweep leaf folders orphaned by a route leaf-count shrink before anything else: this must run even
	// on the otherwise-clean early-return path, since a kRouteSubdivisions edit need not bump kiSplitVersion.
	RemoveOrphanedLeafFolders(routeDirectory, iLeafCount);
	RemoveOrphanedLeafFolders(intermediatesDirectory, iLeafCount);
	RemoveOrphanedLeafFolders(GetIslandDiagnosticsPath(routeDirectory), iLeafCount);

	bool bGaeaDirty = IsGaeaRawDirty(intermediatesDirectory, bakeFingerprint);
	bool bLeavesDirty = AreLeavesDirty(routeDirectory, intermediatesDirectory, iLeafCount, splitFingerprint);
	if (!bGaeaDirty && !bLeavesDirty)
	{
		return;
	}

	std::filesystem::create_directories(intermediatesDirectory);
	// Invalidate split completion before either stage mutates its inputs. A crash after a raw re-bake
	// or midway through overwriting existing leaves must force the split to run again next launch.
	std::filesystem::remove(intermediatesDirectory / kpcSplitVersionFile);
	std::filesystem::path patchedArchetypeFile = intermediatesDirectory / kpcPatchedArchetypeFile;

	// STAGE 1 — Gaea raw export (slow). Skipped when the raw outputs are already present and fresh,
	// so a split-only change re-splits the existing bake without re-running Gaea.
	if (bGaeaDirty)
	{
		// Keep the last complete raw bake available while Gaea runs. Only after every staged output
		// exists do we invalidate completion and publish the new set. A failed Gaea invocation leaves
		// GaeaStaging for diagnosis; the next attempt replaces it before running.
		std::filesystem::path stagingDirectory = intermediatesDirectory / kpcGaeaStagingDirectory;
		std::filesystem::remove_all(stagingDirectory);
		std::filesystem::create_directories(stagingDirectory);
		std::filesystem::path stagingPatchedArchetypeFile = stagingDirectory / kpcPatchedArchetypeFile;
		RunGaeaExport(rContext, rRoute, routeDirectory, stagingDirectory, stagingPatchedArchetypeFile);

		std::filesystem::remove(intermediatesDirectory / kpcBakeVersionFile);
		std::filesystem::remove(intermediatesDirectory / kpcSplitVersionFile);
		for (const char* pcFile : kpcIntermediateFiles)
		{
			std::filesystem::path source = stagingDirectory / pcFile;
			std::filesystem::path destination = intermediatesDirectory / pcFile;
			VERIFY_SUCCESS(MoveFileExW(source.native().c_str(), destination.native().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
		}
		VERIFY_SUCCESS(MoveFileExW(stagingPatchedArchetypeFile.native().c_str(), patchedArchetypeFile.native().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
		std::filesystem::remove_all(stagingDirectory);
		WriteTextFile(intermediatesDirectory / kpcBakeVersionFile, bakeFingerprint);
	}
	else
	{
		LOG(kDefault, kDebug, "Reusing Gaea bake for route \"{}\"; re-splitting only (no Gaea export)", routeDirectory.string());
	}

	// STAGE 2 — split (fast). Raw outputs are present here (just baked, or reused fresh).
	// PatchArchetype doesn't touch the Sea node, so the patched copy keeps the authored Level (or
	// kfGaeaSeaLevelDefault); the per-island beach offset drives the elevation transform, auto-crop
	// cut line, and mesh-Z scrub.
	float fSeaLevelNormalized = ReadArchetypeSeaLevel(patchedArchetypeFile);
	float fBeachOffsetMeters = fSeaLevelNormalized * rContext.rDimensions.fElevationMeters;
	LOG(kDefault, kDebug, "Archetype Sea Level (read, not patched): {} → beach offset {} m for elevationMeters {} m", common::Wb(fSeaLevelNormalized, 4), common::Wb(fBeachOffsetMeters, 2), common::Wb(rContext.rDimensions.fElevationMeters, 2));

	// Auto-crop cut line = -fBeachOffsetMeters + kfCropEpsilonAboveSeaFloorMeters. If the beach
	// offset is at or below the epsilon, the cut line lands at or above sea level and silently
	// strips the entire shoreline halo. Catch the authoring mistake here rather than shipping a
	// halo-less island.
	if (fBeachOffsetMeters <= kfCropEpsilonAboveSeaFloorMeters)
	{
		throw std::runtime_error(std::format("Island \"{}\" beach offset ({:.2f} m, = Sea Level {:.4f} × elevationMeters {:.2f} m) is at or below the crop epsilon ({:.2f} m): the auto-crop would land at or above sea level and strip the shoreline halo. Raise elevationMeters, raise the archetype Sea node's Level, or lower kfCropEpsilonAboveSeaFloorMeters.", rContext.rIslandFolder.string(), fBeachOffsetMeters, fSeaLevelNormalized, rContext.rDimensions.fElevationMeters, kfCropEpsilonAboveSeaFloorMeters));
	}

	// Per-island sea floor must match the engine-wide kfSeaBottomMeters so the elevation RTT clear
	// (Engine/Source/Graphics/Managers/RenderTargetTextures.cpp) blends seamlessly with edge texels.
	ASSERT(std::abs(-fBeachOffsetMeters - common::kfSeaBottomMeters) < 0.01f);

	std::vector<float> fullElevationMeters = LoadElevationMeters(intermediatesDirectory, rContext.iTexturePixels, fSeaLevelNormalized, rContext.rDimensions.fElevationMeters, fBeachOffsetMeters);

	std::vector<uint16_t> fullAmbientOcclusion = LoadAmbientOcclusion(intermediatesDirectory, rContext.iTexturePixels);

	std::vector<float> meshPositions;
	std::vector<uint32_t> meshIndices;
	LoadMesherMesh(intermediatesDirectory, fBeachOffsetMeters, routeDirectory, meshPositions, meshIndices);

	// Split into chunks (up to 1 for 1x1, iColumns × iRows otherwise) and write each leaf that clears
	// the minimum-height threshold (ProcessBakedRegion rejects too-low / underwater chunks). The X / Y
	// region boundaries partition the full texture (uneven when columns/rows don't divide it evenly);
	// ProcessBakedRegion auto-crops within each, borrowing neighbour pixels across a seam for alignment.
	BakeOutput bakeOutput {.rFullElevationMeters = fullElevationMeters, .rFullAmbientOcclusion = fullAmbientOcclusion, .fBeachOffsetMeters = fBeachOffsetMeters};
	int64_t iWrittenLeaves = 0;
	for (int64_t iColumn = 0; iColumn < rRoute.iColumns; ++iColumn)
	{
		for (int64_t iRow = 0; iRow < rRoute.iRows; ++iRow)
		{
			int64_t iChunkIndex = iColumn * rRoute.iRows + iRow;
			RegionBounds region
			{
				.iStartX = iColumn * rContext.iTexturePixels / rRoute.iColumns,
				.iEndX = (iColumn + 1) * rContext.iTexturePixels / rRoute.iColumns,
				.iStartY = iRow * rContext.iTexturePixels / rRoute.iRows,
				.iEndY = (iRow + 1) * rContext.iTexturePixels / rRoute.iRows,
			};
			std::filesystem::path sourceLeafDirectory = routeDirectory / std::to_string(iChunkIndex);
			std::filesystem::path cacheLeafDirectory = intermediatesDirectory / std::to_string(iChunkIndex);
			LeafTarget leaf {.rSourceLeafDirectory = sourceLeafDirectory, .rCacheLeafDirectory = cacheLeafDirectory};
			if (ProcessBakedRegion(rContext, bakeOutput, region, meshPositions, meshIndices, leaf))
			{
				++iWrittenLeaves;
			}
		}
	}

	if (iWrittenLeaves == 0)
	{
		throw std::runtime_error(std::format("Island route \"{}\": every one of {} chunk(s) was rejected as too low (peak < {:.2f} m). Raise Island.json's elevationMeters, lower kfMinIslandMaxHeightMeters, or remove this route from Island.json.", routeDirectory.string(), iLeafCount, kfMinIslandMaxHeightMeters));
	}

	// Stamp the split sentinel last, after every leaf is written. A crash mid-split leaves it absent
	// (or stale), so AreLeavesDirty re-splits next run — without re-running Gaea (BakeVersion is
	// already stamped above, so IsGaeaRawDirty stays clean).
	{
		WriteTextFile(intermediatesDirectory / kpcSplitVersionFile, splitFingerprint);
	}

	LOG(kDefault, kDebug, "Island route \"{}\" ready ({} of {} chunk(s) written, {} rejected as too low{})", routeDirectory.string(), iWrittenLeaves, iLeafCount, iLeafCount - iWrittenLeaves, bGaeaDirty ? ", Gaea re-baked" : ", split-only reuse");
}
