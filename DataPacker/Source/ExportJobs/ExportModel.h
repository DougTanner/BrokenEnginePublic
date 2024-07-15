#pragma once

#include "ExportJob.h"

class ExportModel : public ExportJob
{
public:

	ExportModel(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportModel() = default;

protected:

	virtual void Export();
};
