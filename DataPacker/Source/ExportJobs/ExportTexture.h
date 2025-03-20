#pragma once

#include "ExportJob.h"

class ExportTexture : public ExportJob
{
public:

	static inline constexpr std::string_view kpcName = "Gltf";
	static inline constexpr common::ChunkFlags kChunkFlags = common::ChunkFlags::kGltf;

	static bool Handles(const std::filesystem::directory_entry& rDirectoryEntry)
	{
		std::unordered_set<std::string> extensionSet = {".png", ".tga", ".jpg", ".ktx", ".BC4_UNORM_BLOCK", ".BC7_UNORM_BLOCK", ".R8_UNORM", ".R8G8B8A8_UNORM", ".R16_UNORM", ".R16G16_UNORM", ".R32_SFLOAT"};
		return extensionSet.contains(rDirectoryEntry.path().extension().string());
	}

	ExportTexture(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportTexture() = default;

protected:

	virtual void Export();
};
