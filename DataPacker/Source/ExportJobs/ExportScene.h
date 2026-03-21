#pragma once

#include "tinygltf/tiny_gltf.h"

#include "ExportJob.h"
#include "ExportSceneSkeleton.h"

class ExportScene : public ExportJob
{
public:

	static inline constexpr std::string_view kName = "Scene";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportScene(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportScene() = default;

	virtual int64_t GetVersion() const override { return 50 + sizeof(common::ChunkHeader); }

	virtual bool CheckDirty(const std::filesystem::path& rPackFile) override;

protected:

	virtual void Export() override;

private:

	tinygltf::Model LoadGltfModel();
	std::filesystem::path GetPreExportMarkerPath() const;
	std::filesystem::path GetTextureIntermediatePath(int64_t iTextureIndex, bool bOcclusion) const;

	void PreExport(tinygltf::Model& rGltfModel);
	void MainExport(tinygltf::Model& rGltfModel);

	void CleanupOnFailure() override;

	std::vector<std::filesystem::path> mIntermediateFiles;
};
