#pragma once

#include "ExportJob.h"

class ExportAudio : public ExportJob
{
public:

	ExportAudio(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportAudio() = default;

protected:

	virtual void Export();
};
