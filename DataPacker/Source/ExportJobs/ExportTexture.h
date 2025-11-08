#pragma once

#include "ExportJob.h"

class ExportTexture : public ExportJob
{
public:

	static inline constexpr std::string_view kpcName = "Texture";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	static void AddToHeader(std::fstream& headerFileStream, const std::vector<std::unique_ptr<ExportTexture>>& rExportJobs);

	ExportTexture(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportTexture() = default;

	virtual int64_t GetVersion() const override { return 1 + sizeof(common::ChunkHeader); }

protected:

	virtual void Export() override;
};
