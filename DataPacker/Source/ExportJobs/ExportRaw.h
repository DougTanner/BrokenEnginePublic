#pragma once

#include "ExportJob.h"

class ExportRaw : public ExportJob
{
public:

	static inline constexpr std::string_view kName = "Raw";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportRaw(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportRaw() = default;

	virtual int64_t GetVersion() const override { return Version(1); }

protected:

	virtual void Export() override;
};
