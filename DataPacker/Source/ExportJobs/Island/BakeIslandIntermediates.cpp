#include "BakeIslandIntermediates.h"

#include "BakeIslandIntermediatesInternal.h"
#include "FileManager.h"
#include "GaeaArchetype.h"

namespace
{

std::filesystem::path IslandRelativePath(const std::filesystem::path& rSourcePath)
{
	bool bUnderInputRoot = false;
	for (const std::filesystem::path& rInputRoot : gpFileManager->mpInputDirectories)
	{
		std::error_code error;
		std::filesystem::path relativePath = std::filesystem::relative(rSourcePath, rInputRoot, error);
		if (error || relativePath.empty() || *relativePath.begin() == "..")
		{
			continue;
		}
		bUnderInputRoot = true;
		if (common::ToLower(relativePath.begin()->string()) != "islands")
		{
			continue;
		}
		return relativePath;
	}
	if (bUnderInputRoot)
	{
		throw std::runtime_error(std::format("Island path \"{}\" is not under an input root's Islands directory", rSourcePath.string()));
	}
	throw std::runtime_error(std::format("Island path \"{}\" is outside DataPacker input roots", rSourcePath.string()));
}

// Route → subdivision table (single source of truth). `pcLabel` is BOTH the Island.json "routes"
// value AND the per-route sub-folder name. `iGaeaChoice` is patched into the archetype's single
// Gaea Route node ("Choice") = the 0-based input-port index: 0 = In (1x1, single landmass),
// 1 = Input2 (2x1 dual band), 2 = Input3 (2x2 quad grid), 3 = Input4 (3x1) ... 9 = Input10 (4x4).
// `iColumns`/`iRows` split the full square bake into that many chunks along X (east-west) /
// Y (north-south); each chunk becomes an independent kIsland in the pack.
//
// NOTE: the split axes must match how the Route lays out its landmasses. The "2x1" dual band stacks
// two landmasses along Y (Route Input2 feeds two copies of the cone-edge offset by ±OffsetY), so it
// splits along Y only (iRows = 2). The "2x2" quad grid offsets four copies by ±OffsetX AND ±OffsetY
// into the four quadrant centers, so it splits along both axes (iColumns = iRows = 2) to cut BETWEEN
// the landmasses rather than through them. The label "RxC" reads ROWS-by-COLUMNS (R landmasses along
// Y, C along X); the label names the route, not the pixel axis. Splits need NOT divide texturePixels
// evenly: ProcessBakedRegion searches each natural (possibly uneven) region for its landmass, then
// the per-axis crop borrows neighbour pixels at a non-64-aligned region edge to reach the crop
// alignment, so a 3-way split of a power-of-two bake works. Editing iColumns/iRows is a post-Gaea
// split change: bump kiSplitVersion (NOT kiBakeVersion) so existing Gaea bakes are reused and only
// the split re-runs.
constexpr RouteSubdivision kRouteSubdivisions[] =
{
	{"1x1", 0, 1, 1},
	{"2x1", 1, 1, 2},
	{"2x2", 2, 2, 2},
	{"3x1", 3, 1, 3},
	{"3x2", 4, 2, 3},
	{"3x3", 5, 3, 3},
	{"3x4", 6, 4, 3},
	{"8x4", 7, 4, 8},
	{"4x2", 8, 2, 4},
	{"4x4", 9, 4, 4},
};

const RouteSubdivision& LookupRouteSubdivision(const std::string& rLabel, const std::filesystem::path& rIslandJsonFile)
{
	for (const RouteSubdivision& rRoute : kRouteSubdivisions)
	{
		if (rLabel == rRoute.pcLabel)
		{
			return rRoute;
		}
	}

	std::string validLabels;
	for (const RouteSubdivision& rRoute : kRouteSubdivisions)
	{
		if (!validLabels.empty())
		{
			validLabels += ", ";
		}
		validLabels += rRoute.pcLabel;
	}
	throw std::runtime_error(std::format("\"{}\" lists unknown route \"{}\". Valid routes: {}.", rIslandJsonFile.string(), rLabel, validLabels));
}

std::filesystem::path ResolveTerrain(const std::filesystem::path& rIslandFolder, const nlohmann::json& rIslandJson)
{
	// Named archetype: two-tier lookup — any input dir's shared Islands/ folder, then sibling in
	// the island folder. Iterates all input directories so archetypes can live anywhere on the
	// search path.
	std::string archetype = rIslandJson.at("archetype").get<std::string>();

	for (const std::filesystem::path& rInputDirectory : gpFileManager->mpInputDirectories)
	{
		std::filesystem::path sharedPath = rInputDirectory / "Islands" / (archetype + ".terrain");
		if (std::filesystem::exists(sharedPath))
		{
			return sharedPath;
		}
	}

	std::filesystem::path siblingPath = rIslandFolder / (archetype + ".terrain");
	if (std::filesystem::exists(siblingPath))
	{
		return siblingPath;
	}

	throw std::runtime_error(std::format("Archetype '{}' not found in any input directory's Islands/ folder or as a sibling of \"{}\".", archetype, rIslandFolder.string()));
}

void BakeOne(const std::filesystem::path& rGaeaExecutable, const std::filesystem::path& rIslandFolder)
{
	std::filesystem::path islandJsonFile = rIslandFolder / "Island.json";

	std::ifstream islandStream(islandJsonFile);
	nlohmann::json islandJson = nlohmann::json::parse(islandStream);
	islandStream.close();

	if (!islandJson.is_object())
	{
		throw std::runtime_error(std::format("\"{}\" must be a JSON object", islandJsonFile.string()));
	}

	// All schema fields are strictly required. Catches stale configs (e.g., legacy "mips" key) and
	// avoids silent implicit-zero seeds or unscaled archetype dimensions.
	for (const char* pcRequired : kpcRequiredIslandJsonKeys)
	{
		if (!islandJson.contains(pcRequired))
		{
			throw std::runtime_error(std::format("\"{}\" is missing required key \"{}\". Schema is: archetype (string), seed (int), widthMeters (float), elevationMeters (float), texturePixels (int), routes (array of label strings, e.g. [\"1x1\", \"2x1\"]).", islandJsonFile.string(), pcRequired));
		}
	}

	if (islandJson.contains("mips"))
	{
		throw std::runtime_error(std::format("\"{}\" has legacy \"mips\" key. Islands no longer use mip chains: remove \"mips\" and use \"texturePixels\" instead (single resolution; elevation auto-downsamples to texturePixels / {}).", islandJsonFile.string(), kiElevationDivisor));
	}

	int32_t iSeed = islandJson.at("seed").get<int32_t>();
	int64_t iTexturePixels = islandJson.at("texturePixels").get<int64_t>();
	WorldDimensions dimensions
	{
		.fFootprintMeters = islandJson.at("widthMeters").get<float>(),
		.fElevationMeters = islandJson.at("elevationMeters").get<float>(),
	};

	// Optional: override the Mesher's VerticesPerSide (per-island mesh density). When absent, leave
	// whatever value the archetype's Mesher node already has. Stripped from varsJson so it doesn't
	// reach Gaea as a graph variable.
	std::optional<int64_t> oiMeshResolution;
	if (islandJson.contains("meshResolution"))
	{
		oiMeshResolution = islandJson.at("meshResolution").get<int64_t>();
	}

	// texturePixels must be >= and a multiple of kiCropAlignment so the per-axis auto-crop dims
	// satisfy both BC block alignment and Vulkan transfer-queue granularity (block-relative for
	// compressed formats) on every BC + elevation upload. See kiCropAlignment's definition.
	if (iTexturePixels < kiCropAlignment || (iTexturePixels % kiCropAlignment) != 0)
	{
		throw std::runtime_error(std::format("\"{}\" texturePixels {} is invalid: must be >= {} and a multiple of {} so the auto-crop dims stay BC-block- and Vulkan-transfer-granularity-aligned.", islandJsonFile.string(), iTexturePixels, kiCropAlignment, kiCropAlignment));
	}

	// Resolve the routes list (one or many labels). Each must exist in kRouteSubdivisions; a split
	// route additionally requires every per-column / per-row pixel span stay >= the crop alignment.
	const nlohmann::json& rRoutesJson = islandJson.at("routes");
	if (!rRoutesJson.is_array() || rRoutesJson.empty())
	{
		throw std::runtime_error(std::format("\"{}\" \"routes\" must be a non-empty array of label strings (e.g. [\"1x1\", \"2x1\"]).", islandJsonFile.string()));
	}
	std::vector<const RouteSubdivision*> routes;
	for (const nlohmann::json& rRouteJson : rRoutesJson)
	{
		if (!rRouteJson.is_string())
		{
			throw std::runtime_error(std::format("\"{}\" \"routes\" entries must be label strings (e.g. \"1x1\").", islandJsonFile.string()));
		}
		const RouteSubdivision& rRoute = LookupRouteSubdivision(rRouteJson.get<std::string>(), islandJsonFile);
		// Splits need not divide texturePixels evenly: ProcessBakedRegion searches each natural
		// (possibly uneven) region for its landmass, then the per-axis crop borrows neighbour pixels at
		// a non-64-aligned region edge to reach kiCropAlignment. The only requirement is that every
		// natural per-chunk span stays >= kiCropAlignment, so each region is large enough to hold an
		// aligned crop (texturePixels is already a multiple of kiCropAlignment, so 1x1 always passes).
		if ((iTexturePixels / rRoute.iColumns) < kiCropAlignment
			|| (iTexturePixels / rRoute.iRows) < kiCropAlignment)
		{
			throw std::runtime_error(std::format("\"{}\" route \"{}\" splits texturePixels {} into {}x{} chunks, but a per-chunk pixel span ({}x{}) would be smaller than the {}-pixel crop alignment. Lower the subdivision or raise texturePixels.", islandJsonFile.string(), rRoute.pcLabel, iTexturePixels, rRoute.iColumns, rRoute.iRows, iTexturePixels / rRoute.iColumns, iTexturePixels / rRoute.iRows, kiCropAlignment));
		}
		routes.push_back(&rRoute);
	}

	std::filesystem::path archetypeFile = ResolveTerrain(rIslandFolder, islandJson);
	std::filesystem::path cacheIslandFolder = GetIslandCachePath(rIslandFolder);

	// Prune stale source sub-folders: any directory whose name isn't a current route label. This
	// drops folders for routes removed from the list, so no orphaned leaf produces a stale chunk.
	// Island.json and the archetype .terrain are files, so this never touches them.
	// Collect stale sub-folders first, then delete — mutating the directory mid-iteration via
	// remove_all is unspecified behavior for directory_iterator.
	std::vector<std::filesystem::path> staleSubFolders;
	for (const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(rIslandFolder))
	{
		if (!rEntry.is_directory())
		{
			continue;
		}
		std::string name = rEntry.path().filename().string();
		bool bIsRouteLabel = false;
		for (const RouteSubdivision* pRoute : routes)
		{
			if (name == pRoute->pcLabel)
			{
				bIsRouteLabel = true;
				break;
			}
		}
		if (!bIsRouteLabel)
		{
			staleSubFolders.push_back(rEntry.path());
		}
	}
	for (const std::filesystem::path& rStaleSubFolder : staleSubFolders)
	{
		std::filesystem::remove_all(rStaleSubFolder);
		LOG(kDefault, kDebug, "Removed stale island sub-folder: \"{}\"", rStaleSubFolder.string());
	}
	std::function<void(const std::filesystem::path&)> pruneStaleCacheRoutes = [&routes](const std::filesystem::path& rCacheIslandFolder)
	{
		if (!std::filesystem::exists(rCacheIslandFolder))
		{
			return;
		}
		std::vector<std::filesystem::path> staleCacheRoutes;
		for (const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(rCacheIslandFolder))
		{
			if (!rEntry.is_directory())
			{
				continue;
			}
			bool bCurrentRoute = std::ranges::any_of(routes, [&rEntry](const RouteSubdivision* pRoute)
			{
				return rEntry.path().filename() == pRoute->pcLabel;
			});
			if (!bCurrentRoute)
			{
				staleCacheRoutes.push_back(rEntry.path());
			}
		}
		for (const std::filesystem::path& rStaleCacheRoute : staleCacheRoutes)
		{
			std::filesystem::remove_all(rStaleCacheRoute);
			LOG(kDefault, kDebug, "Removed stale Gaea cache route: \"{}\"", rStaleCacheRoute.string());
		}
	};
	pruneStaleCacheRoutes(cacheIslandFolder);
	pruneStaleCacheRoutes(GetIslandDiagnosticsPath(rIslandFolder));

	IslandBakeContext context
	{
		.rGaeaExecutable = rGaeaExecutable,
		.rIslandFolder = rIslandFolder,
		.rCacheIslandFolder = cacheIslandFolder,
		.rArchetypeFile = archetypeFile,
		.rIslandJsonFile = islandJsonFile,
		.rIslandJson = islandJson,
		.rDimensions = dimensions,
		.iSeed = iSeed,
		.iTexturePixels = iTexturePixels,
		.oiMeshResolution = oiMeshResolution,
	};
	for (const RouteSubdivision* pRoute : routes)
	{
		BakeRoute(context, *pRoute);
	}
}

} // namespace

std::filesystem::path GetIslandCachePath(const std::filesystem::path& rSourcePath)
{
	return gpFileManager->mGaeaCacheDirectory / IslandRelativePath(rSourcePath);
}

std::filesystem::path GetIslandDiagnosticsPath(const std::filesystem::path& rSourcePath)
{
	return gpFileManager->mGaeaCacheDirectory / "Diagnostics" / IslandRelativePath(rSourcePath);
}

BakedDimensions ReadBakedDimensions(const std::filesystem::path& rLeafFolder)
{
	std::filesystem::path bakedJsonFile = GetIslandCachePath(rLeafFolder) / kpcBakedDimensionsFile;
	std::ifstream bakedStream(bakedJsonFile);
	nlohmann::json bakedJson = nlohmann::json::parse(bakedStream);
	bakedStream.close();

	return BakedDimensions
	{
		.fWidthMeters = bakedJson.at("widthMeters").get<float>(),
		.fHeightMeters = bakedJson.at("heightMeters").get<float>(),
		.fElevationMeters = bakedJson.at("elevationMeters").get<float>(),
		.iCropX = bakedJson.at("cropX").get<int64_t>(),
		.iCropY = bakedJson.at("cropY").get<int64_t>(),
		.iCropWidth = bakedJson.at("cropWidth").get<int64_t>(),
		.iCropHeight = bakedJson.at("cropHeight").get<int64_t>(),
		.iFullTexturePixels = bakedJson.at("fullTexturePixels").get<int64_t>(),
	};
}

void BakeIslandIntermediates()
{
	std::vector<std::filesystem::path> islandFolders;
	for (const std::filesystem::path& rInputDirectory : gpFileManager->mpInputDirectories)
	{
		std::filesystem::path islandsRoot = rInputDirectory / "Islands";
		if (!std::filesystem::exists(islandsRoot))
		{
			continue;
		}

		for (const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(islandsRoot))
		{
			if (rEntry.is_directory() && std::filesystem::exists(rEntry.path() / "Island.json"))
			{
				islandFolders.push_back(rEntry.path());
			}
		}
	}

	if (islandFolders.empty())
	{
		return;
	}

	std::sort(islandFolders.begin(), islandFolders.end());

	std::filesystem::path gaeaExecutable = ResolveGaeaExecutable();

	for (const std::filesystem::path& rIslandFolder : islandFolders)
	{
		BakeOne(gaeaExecutable, rIslandFolder);
	}
}
