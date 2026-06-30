#pragma once

#include "ExportJob.h"

class ExportModel : public ExportJob
{
public:

	static inline constexpr std::string_view kName = "Model";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportModel(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportModel() = default;

	// Payload-struct sizes fold in so size-changing layout edits auto-dirty cached chunks; same-size reorders need the raw version bumped
	virtual int64_t GetVersion() const override { return Version(2 + sizeof(common::ModelVertex) + sizeof(common::MaterialInfo)); }

protected:

	virtual void Export() override;
};
