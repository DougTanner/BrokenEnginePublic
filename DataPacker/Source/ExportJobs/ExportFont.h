#pragma once

#include "ExportJob.h"

class ExportFont : public ExportJob
{
public:

	ExportFont(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportFont() = default;

protected:

	virtual void Export();
};
