#pragma once

namespace tinygltf { class Model; }

#include "ExportJob.h"
#include "Scene/SceneSkeletonLoader.h"
#include "Scene/SceneVerticesLoader.h"

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

	// Payload-struct sizes fold in so size-changing layout edits auto-dirty cached chunks and the .PreExport marker; same-size reorders need the raw version bumped
	virtual int64_t GetVersion() const override
	{
		return Version(53
			+ sizeof(common::MaterialShaderData)
			+ sizeof(common::AnimationHeader)
			+ sizeof(common::Skeleton)
			+ sizeof(common::ModelNode)
			+ sizeof(common::AnimationClip)
			+ sizeof(common::MaterialInfo)
			+ sizeof(common::AnimationChannel)
			+ sizeof(common::AnimationKeyframe)
			+ sizeof(common::AnimationKeyframeCubic)
			+ sizeof(common::ModelVertex));
	}

	virtual bool CheckDirty(const std::filesystem::path& rPackFile) override;

protected:

	virtual void Export() override;

private:

	tinygltf::Model LoadGltfModel();
	std::filesystem::path GetPreExportMarkerPath() const;
	std::filesystem::path GetTextureIntermediatePath(int64_t iTextureIndex, VkFormat vkFormat) const;

	void PreExport(tinygltf::Model& rGltfModel);
	void MainExport(tinygltf::Model& rGltfModel);

	void ProcessTextures(tinygltf::Model& rGltfModel);
	void SetupSkeletonAndMaterials(tinygltf::Model& rGltfModel, std::unordered_map<int, int>& rNodeToJointMap);
	void LoadVerticesAndOptimizeMeshes(tinygltf::Model& rGltfModel, const std::unordered_map<int, int>& rNodeToJointMap, std::vector<Material>& rMaterials, std::vector<MaterialNodeInfo>& rMaterialNodeInfos, std::vector<common::ModelVertex>& rVertices);
	void BuildMaterialInfos(tinygltf::Model& rGltfModel, const std::unordered_map<int, int>& rNodeToJointMap, const std::vector<Material>& rMaterials, const std::vector<MaterialNodeInfo>& rMaterialNodeInfos, std::vector<common::MaterialInfo>& rMaterialInfos);
	void WriteModelFile(const std::vector<Material>& rMaterials, const std::vector<common::MaterialInfo>& rMaterialInfos, std::vector<common::ModelVertex>& rVertices);

	void ReadMaterialInfosFromModel(const std::filesystem::path& rModelPath, size_t uiMaterialCount, uint32_t* puiIndexStarts, std::vector<common::MaterialInfo>& rMaterialInfos);
	void FillMaterialShaderDatas(tinygltf::Model& rGltfModel, const std::vector<common::MaterialInfo>& rMaterialInfos, common::MaterialShaderData* pMaterialShaderDatas);
	void WriteAnimationSection(tinygltf::Model& rGltfModel, const std::vector<common::MaterialInfo>& rMaterialInfos, common::ChunkHeader* pHeader, int64_t iSceneArraysSize);

	void CleanupOnFailure() override;

	std::vector<std::filesystem::path> mIntermediateFiles;
};
