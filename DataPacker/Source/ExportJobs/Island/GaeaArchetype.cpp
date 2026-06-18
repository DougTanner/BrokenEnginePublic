#include "GaeaArchetype.h"

namespace
{

constexpr const wchar_t* kpwcGaeaDefaultPath = L"C:\\Program Files\\QuadSpinner\\Gaea 2\\Gaea.Swarm.exe";
constexpr const char* kpcGaeaEnvVar = "GAEA2_PATH";

// Fallback assumption for the Gaea Sea node's normalized `Level` field. Gaea omits the key
// from .terrain JSON when it equals the editor default (~0.0995); we treat the missing case as
// 0.1 so DataPacker matches what the Gaea editor preview shows. Archetypes that author Level
// explicitly override this fallback. DataPacker never patches Level — it is read once per bake
// in BakeOne via ReadArchetypeSeaLevel and multiplied by elevationMeters to derive the per-island
// beach offset (engine-Z 0 == beach; sea floor sits at -(Level × elevationMeters)).
constexpr float kfGaeaSeaLevelDefault = 0.1f;

void WriteFileBytes(const std::filesystem::path& rFile, const std::string& rBytes)
{
	// Atomic replace: partial write on crash leaves a stray .tmp, not a half-written archetype.
	// PID suffix so concurrent crashes from peer DataPacker processes leave distinct orphan
	// .<pid>.tmp files instead of clobbering each other's in-flight writes.
	std::filesystem::path tempFile = rFile;
	tempFile += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
	{
		std::ofstream stream(tempFile, std::ios::binary);
		stream.write(rBytes.data(), rBytes.size());
		stream.close();
		VERIFY_SUCCESS(stream.good());
	}
	std::filesystem::rename(tempFile, rFile);
}

// Recursive walker: every object whose key is exactly "Seed" with a numeric value is overwritten
// with iSeed. Gaea's `--seed` CLI flag only mixes a global seed into per-node randomness; it
// doesn't override the per-node "Seed" fields baked into the .terrain JSON. Patching them
// directly is the only way to make Island.json's seed value fully determine the bake.
void PatchArchetypeSeeds(nlohmann::json& rJson, int32_t iSeed)
{
	if (rJson.is_object())
	{
		for (auto& [rKey, rValue] : rJson.items())
		{
			if (rKey == "Seed" && rValue.is_number())
			{
				rValue = iSeed;
			}
			else
			{
				PatchArchetypeSeeds(rValue, iSeed);
			}
		}
	}
	else if (rJson.is_array())
	{
		for (nlohmann::json& rChild : rJson)
		{
			PatchArchetypeSeeds(rChild, iSeed);
		}
	}
}

// Recursive walker: every node whose $type contains "Mesher" gets its VerticesPerSide set. Gaea's
// default Mesher resolution is implicit (inherits BakeResolution) so the property may be absent
// from the JSON — we create the key when missing. Restored via the standard archetype-bytes rollback.
void PatchArchetypeMesherResolution(nlohmann::json& rJson, int64_t iVerticesPerSide)
{
	if (rJson.is_object())
	{
		auto it = rJson.find("$type");
		if (it != rJson.end() && it->is_string() && it->get<std::string>().find("Mesher") != std::string::npos)
		{
			rJson["VerticesPerSide"] = iVerticesPerSide;
		}
		for (auto& [rKey, rValue] : rJson.items())
		{
			PatchArchetypeMesherResolution(rValue, iVerticesPerSide);
		}
	}
	else if (rJson.is_array())
	{
		for (nlohmann::json& rChild : rJson)
		{
			PatchArchetypeMesherResolution(rChild, iVerticesPerSide);
		}
	}
}

// Recursive walker: finds the single node whose $type names a Gaea Route node and sets its
// "Choice" (0 = 1x1 single landmass, 1 = 2x1 dual band, 2 = 2x2 quad grid — see kRouteSubdivisions
// in BakeIslandIntermediates.cpp). The on-disk
// archetype authors Choice at whatever the editor was last saved at; DataPacker always patches it
// per route so the bake is deterministic. riRouteNodeCount accumulates matches so the caller can
// assert exactly one Route node exists.
void PatchArchetypeRoute(nlohmann::json& rJson, int32_t iChoice, int64_t& riRouteNodeCount)
{
	if (rJson.is_object())
	{
		auto it = rJson.find("$type");
		if (it != rJson.end() && it->is_string() && it->get<std::string>().starts_with("QuadSpinner.Gaea.Nodes.Route,"))
		{
			rJson["Choice"] = iChoice;
			++riRouteNodeCount;
		}
		for (auto& [rKey, rValue] : rJson.items())
		{
			PatchArchetypeRoute(rValue, iChoice, riRouteNodeCount);
		}
	}
	else if (rJson.is_array())
	{
		for (nlohmann::json& rChild : rJson)
		{
			PatchArchetypeRoute(rChild, iChoice, riRouteNodeCount);
		}
	}
}

} // namespace

std::filesystem::path ResolveGaeaExecutable()
{
	char* pcEnvValue = nullptr;
	size_t uiEnvSize = 0;
	_dupenv_s(&pcEnvValue, &uiEnvSize, kpcGaeaEnvVar);
	if (pcEnvValue != nullptr)
	{
		std::filesystem::path envPath(pcEnvValue);
		std::free(pcEnvValue);
		if (std::filesystem::exists(envPath))
		{
			return envPath;
		}
	}

	std::filesystem::path defaultPath(kpwcGaeaDefaultPath);
	if (std::filesystem::exists(defaultPath))
	{
		return defaultPath;
	}

	throw std::runtime_error(std::format("Gaea.Swarm.exe not found. Set {} env var or install Gaea 2 to the default location ({}).", kpcGaeaEnvVar, std::filesystem::path(kpwcGaeaDefaultPath).string()));
}

float ReadArchetypeSeaLevel(const std::filesystem::path& rTerrainFile)
{
	std::ifstream readStream(rTerrainFile);
	nlohmann::json terrainJson = nlohmann::json::parse(readStream);
	readStream.close();

	const nlohmann::json& rNodes = terrainJson.at("Assets").at("$values").at(0).at("Terrain").at("Nodes");
	for (const auto& [rKey, rNode] : rNodes.items())
	{
		if (!rNode.is_object() || !rNode.contains("$type"))
		{
			continue;
		}
		std::string type = rNode.at("$type").get<std::string>();
		if (type.starts_with("QuadSpinner.Gaea.Nodes.Sea"))
		{
			return rNode.value("Level", kfGaeaSeaLevelDefault);
		}
	}
	throw std::runtime_error("Archetype has no Sea node — DataPacker reads its Level to derive the per-island beach offset. Add a Sea node to the graph or extend BakeIslandIntermediates to handle sea-less archetypes.");
}

void PatchArchetype(const std::filesystem::path& rTerrainFile, const WorldDimensions& rDimensions, int32_t iSeed, std::optional<int64_t> oiMeshResolution, int32_t iGaeaRouteChoice)
{
	std::ifstream readStream(rTerrainFile);
	nlohmann::json terrainJson = nlohmann::json::parse(readStream);
	readStream.close();

	// Layout: Assets["$values"][0].Terrain.{Width, Height}. Verified against Island-1x1.terrain.
	nlohmann::json& rTerrain = terrainJson.at("Assets").at("$values").at(0).at("Terrain");
	rTerrain.at("Width") = rDimensions.fFootprintMeters;
	rTerrain.at("Height") = rDimensions.fElevationMeters;

	int64_t iRouteNodeCount = 0;
	PatchArchetypeRoute(terrainJson, iGaeaRouteChoice, iRouteNodeCount);
	if (iRouteNodeCount != 1)
	{
		throw std::runtime_error(std::format("Archetype \"{}\" has {} Gaea Route node(s); DataPacker expects exactly 1 to set the per-route subdivision Choice. Add a Route node to the graph (Choice 0 = 1x1, 1 = 2x1) or extend BakeIslandIntermediates for multi-Route archetypes.", rTerrainFile.string(), iRouteNodeCount));
	}

	if (iSeed != 0)
	{
		PatchArchetypeSeeds(terrainJson, iSeed);
	}

	if (oiMeshResolution.has_value())
	{
		PatchArchetypeMesherResolution(terrainJson, *oiMeshResolution);
	}

	WriteFileBytes(rTerrainFile, terrainJson.dump(2));
}
