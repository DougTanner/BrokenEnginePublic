#pragma once

#include "ExportJob.h"

class ExportTexture : public ExportJob
{
public:

	ExportTexture(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportTexture() = default;

protected:

	virtual void Export();
};
