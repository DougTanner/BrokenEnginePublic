#pragma once

#include "ExportJob.h"

class ExportAudio : public ExportJob
{
public:

	static inline constexpr std::string_view kpcName = "Audio";
	static inline constexpr common::ChunkFlags kChunkFlags = common::ChunkFlags::kAudio;

	static bool Handles(const std::filesystem::directory_entry& rDirectoryEntry)
	{
		return rDirectoryEntry.path().extension() == ".wav";
	}

	ExportAudio(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportAudio() = default;

protected:

	virtual void Export() override;
};
