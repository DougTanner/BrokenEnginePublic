#pragma once

#include "ExportJob.h"

class ExportFont : public ExportJob
{
public:

	static inline constexpr std::string_view kpcName = "Font";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportFont(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportFont() = default;

protected:

	virtual void Export() override;
};
