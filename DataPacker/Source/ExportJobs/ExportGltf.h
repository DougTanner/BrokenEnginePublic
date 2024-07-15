#pragma once

#include "tinygltf/tiny_gltf.h"

#include "ExportJob.h"

class ExportGltf : public ExportJob
{
public:

	ExportGltf(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportGltf() = default;

protected:

	virtual void PreExport();
	virtual void Export();

private:

	tinygltf::Model LoadGltfModel();
};
