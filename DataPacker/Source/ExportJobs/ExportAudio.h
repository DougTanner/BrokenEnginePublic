#pragma once

#include "ExportJob.h"

class ExportAudio : public ExportJob
{
public:

	static inline constexpr std::string_view kpcName = "Audio";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportAudio(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportAudio() = default;

	virtual int64_t GetVersion() const override { return 1; }

protected:

	virtual void Export() override;
};
