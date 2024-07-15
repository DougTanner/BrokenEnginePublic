#pragma once

#include "ExportJob.h"

class ExportShader : public ExportJob
{
public:

	ExportShader(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportShader() = default;

protected:

	virtual void Export();
};
