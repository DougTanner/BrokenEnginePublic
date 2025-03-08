#pragma once

#include "tinygltf/tiny_gltf.h"

#include "ExportJob.h"

class ExportGltf : public ExportJob
{
public:

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

	virtual void Export();

private:

	tinygltf::Model LoadGltfModel();
};
