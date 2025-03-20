#pragma once

#include "ExportJob.h"

class ExportModel : public ExportJob
{
public:

	static inline constexpr std::string_view kpcName = "Model";
	static inline constexpr common::ChunkFlags kChunkFlags = common::ChunkFlags::kModel;

	static bool Handles(const std::filesystem::directory_entry& rDirectoryEntry)
	{
		return rDirectoryEntry.path().extension() == ".obj" || rDirectoryEntry.path().extension() == ".GLTF_MODEL";
	}

	ExportModel(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportModel() = default;

protected:

	virtual void Export();
};
