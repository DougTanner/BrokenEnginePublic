#pragma once

#include "ExportJob.h"

class ExportFont : public ExportJob
{
public:

	static inline constexpr std::string_view kName = "Font";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportFont(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportFont() = default;

	// Payload-struct size folds in so size-changing layout edits auto-dirty cached chunks; same-size reorders need the raw version bumped
	virtual int64_t GetVersion() const override { return Version(1 + sizeof(common::Character)); }

protected:

	virtual void Export() override;
};
