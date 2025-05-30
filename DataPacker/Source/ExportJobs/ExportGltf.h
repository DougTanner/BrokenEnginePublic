#pragma once

#include "tinygltf/tiny_gltf.h"

#include "DataFile.h"
#include "ExportJob.h"

class ExportGltf : public ExportJob
{
public:

	static inline constexpr std::string_view kpcName = "Gltf";
	static inline constexpr common::ChunkFlags kChunkFlags = common::ChunkFlags::kGltf;

	static bool Handles(const std::filesystem::directory_entry& rDirectoryEntry)
	{
		return rDirectoryEntry.path().extension() == ".gltf";
	}

	ExportGltf(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportGltf() = default;

protected:

	virtual void Export() override;

private:

	tinygltf::Model LoadGltfModel();
};
