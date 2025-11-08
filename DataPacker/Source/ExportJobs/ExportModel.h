#pragma once

#include "ExportJob.h"

class ExportModel : public ExportJob
{
public:

	static inline constexpr std::string_view kpcName = "Model";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportModel(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportModel() = default;

	virtual int64_t GetVersion() const override { return 1 + sizeof(common::ChunkHeader); }

protected:

	virtual void Export() override;
};
