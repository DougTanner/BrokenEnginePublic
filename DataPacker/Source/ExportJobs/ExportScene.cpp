#include "ExportScene.h"

#include "ExportSceneAnimation.h"
#include "ExportSceneSkeleton.h"
#include "ExportSceneVertices.h"
#include "Texture.h"

using enum common::ChunkFlags;

tinygltf::TinyGLTF gGltfContext;

std::optional<common::ChunkFlags_t> ExportScene::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	return rDirectoryEntry.path().extension() == ".gltf" ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kScene) : std::nullopt;
}

bool ExportScene::CheckDirty(const std::filesystem::path& rPackFile)
{
	bool bDirty = ExportJob::CheckDirty(rPackFile);

	if (bDirty)
	{
		// If the main file is dirty, invalidate pre-export cache
		std::filesystem::path preExportPath = GetPreExportMarkerPath();
		if (std::filesystem::exists(preExportPath))
		{
			Log("Removing pre-export marker due to dirty main file");
			std::filesystem::remove(preExportPath);
		}
	}
	else
	{
		// Check if pre-export marker is missing or has wrong version
		std::filesystem::path preExportPath = GetPreExportMarkerPath();
		if (!std::filesystem::exists(preExportPath))
		{
			mbDirty = true;
			bDirty = true;
		}
		else
		{
			std::fstream fileStreamIn(preExportPath, std::ios::in | std::ios::binary);
			int64_t iStoredVersion = 0;
			fileStreamIn.read(reinterpret_cast<char*>(&iStoredVersion), sizeof(iStoredVersion));
			if (iStoredVersion != GetVersion())
			{
				mbDirty = true;
				bDirty = true;
			}
		}
	}

	return bDirty;
}

VkFilter ToVkFilter(int iFilterMode)
{
	switch (iFilterMode)
	{
		case 9728:
			return VK_FILTER_NEAREST;
		case 9729:
			return VK_FILTER_LINEAR;
		case 9984:
			return VK_FILTER_NEAREST;
		case 9985:
			return VK_FILTER_NEAREST;
		case 9986:
			return VK_FILTER_LINEAR;
		case 9987:
			return VK_FILTER_LINEAR;
		case -1:
			return VK_FILTER_LINEAR;
		default:
			DEBUG_BREAK();
			return VK_FILTER_LINEAR;
	}
}

VkSamplerAddressMode ToVkSamplerAddressMode(int iWrapMode)
{
	switch (iWrapMode)
	{
		case 10497:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case 33071:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case 33648:
			return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case -1:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		default:
			DEBUG_BREAK();
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

std::filesystem::path ExportScene::GetPreExportMarkerPath() const
{
	std::filesystem::path path(mInputPath);
	path += ".PreExport";
	return path;
}

std::filesystem::path ExportScene::GetTextureIntermediatePath(int64_t iTextureIndex, bool bOcclusion) const
{
	std::filesystem::path path(mInputPath);
	path += ".Texture";
	path += std::to_string(iTextureIndex);
	path += bOcclusion ? ".BC4_UNORM_BLOCK" : ".BC7_UNORM_BLOCK";
	return path;
}

tinygltf::Model ExportScene::LoadGltfModel()
{
	tinygltf::Model gltfModel;

	std::string filename = mInputPath.string();
	size_t uiExtensionPosition = filename.rfind('.', filename.length());
	bool bBinary = false;
	if (uiExtensionPosition != std::string::npos)
	{
		bBinary = (filename.substr(uiExtensionPosition + 1, filename.length() - uiExtensionPosition) == "glb");
	}

	std::string error;
	std::string warning;
	// Load glTF model from file (binary or ASCII)
	bool bFileLoaded = bBinary ? gGltfContext.LoadBinaryFromFile(&gltfModel, &error, &warning, filename.c_str()) : gGltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename.c_str());
	if (!bFileLoaded)
	{
		throw std::runtime_error(std::format("Failed to load GLTF model '{}': {} (warning: {})", filename, error, warning));
	}

	return gltfModel;
}

void ExportScene::Export()
{
	tinygltf::Model gltfModel = LoadGltfModel();

	std::filesystem::path preExportPath = GetPreExportMarkerPath();
	bool bNeedsPreExport = true;
	if (std::filesystem::exists(preExportPath))
	{
		std::fstream fileStreamIn(preExportPath, std::ios::in | std::ios::binary);
		int64_t iStoredVersion = 0;
		fileStreamIn.read(reinterpret_cast<char*>(&iStoredVersion), sizeof(iStoredVersion));
		bNeedsPreExport = (iStoredVersion != GetVersion());
	}

	if (bNeedsPreExport)
	{
		PreExport(gltfModel);
	}

	MainExport(gltfModel);
}

void ExportScene::PreExport(tinygltf::Model& rGltfModel)
{
	Log("PreExport Gltf: {}", mInputPath.string());

	ASSERT(rGltfModel.textures.size() <= common::SceneHeader::kiMaxTextures);
	Log("Pre-processing {} textures", rGltfModel.textures.size());

	// Pre-compute occlusion flags
	std::vector<bool> occlusionFlags;
	occlusionFlags.reserve(rGltfModel.textures.size());
	for (const tinygltf::Texture& rTexture : rGltfModel.textures)
	{
		bool bOcclusion = false;
		for (const tinygltf::Material& rMaterial : rGltfModel.materials)
		{
			if (IsOcclusion(rTexture.source, rMaterial))
			{
				bOcclusion = true;
				break;
			}
		}
		occlusionFlags.push_back(bOcclusion);
	}

	// Launch async texture processing tasks
	std::vector<std::future<void>> futures;
	futures.reserve(rGltfModel.textures.size());
	for (size_t i = 0; i < rGltfModel.textures.size(); ++i)
	{
		bool bOcclusion = occlusionFlags.at(i);
		const tinygltf::Image& rImage = rGltfModel.images.at(rGltfModel.textures.at(i).source);
		std::filesystem::path path = GetTextureIntermediatePath(rGltfModel.textures.at(i).source, bOcclusion);

		futures.push_back(std::async(std::launch::async, [bOcclusion, &rImage, path]()
		{
			VkFormat vkFormat = bOcclusion ? VK_FORMAT_BC4_UNORM_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
			Texture texture(reinterpret_cast<const std::byte*>(rImage.image.data()), rImage.width, rImage.height, rImage.component);
			texture.MakeMipmaps(vkFormat);
			texture.Save(path, vkFormat, false);
		}));
	}

	// Wait for all tasks and log results
	int64_t iTextureIndex = 0;
	for (size_t i = 0; i < rGltfModel.textures.size(); ++i)
	{
		futures.at(i).get();
		const tinygltf::Image& rImage = rGltfModel.images.at(rGltfModel.textures.at(i).source);
		std::filesystem::path path = GetTextureIntermediatePath(rGltfModel.textures.at(i).source, occlusionFlags.at(i));
		mIntermediateFiles.push_back(path);
		Log("  {}: Texture {} -> {}", iTextureIndex++, rImage.uri, path.filename().native());
	}

	std::filesystem::path path(mInputPath);
	path += ".MODEL";
	Log("Loading {} materials", rGltfModel.materials.size());
	std::vector<Material> materials(rGltfModel.materials.size());
	std::vector<MaterialNodeInfo> materialNodeInfos(rGltfModel.materials.size());

	// Build nodeToNodeIndexMap - identity mapping since we store all nodes
	std::unordered_map<int, int> nodeToJointMap;
	bool bNodeBasedAnimation = false;

	bool bUseSkeletalAnimation = DetermineAnimationPath(rGltfModel);

	if (bUseSkeletalAnimation)
	{
		// Identity mapping - all nodes stored
		for (int64_t i = 0; i < static_cast<int64_t>(rGltfModel.nodes.size()); ++i)
		{
			nodeToJointMap[static_cast<int>(i)] = static_cast<int>(i);
		}
		Log("  Skeletal animation detected: {} skin joints, {} total nodes", rGltfModel.skins[0].joints.size(), rGltfModel.nodes.size());
	}
	else if (rGltfModel.animations.size() > 0)
	{
		// Node-based animation: build skeleton from node hierarchy (also uses identity mapping)
		bNodeBasedAnimation = true;
		SkeletonData tempSkeletonData = BuildNodeSkeleton(rGltfModel, nodeToJointMap);
		Log("  Node-based animation detected: {} nodes in skeleton", tempSkeletonData.skeleton.uiNodeCount);
	}

	const tinygltf::Scene& rScene = rGltfModel.scenes[rGltfModel.defaultScene > -1 ? rGltfModel.defaultScene : 0];
	std::vector<common::ModelVertex> vertices;
	std::unordered_map<std::pair<int, int>, int, PairHash> materialNodeMap;
	for (size_t i = 0; i < rScene.nodes.size(); ++i)
	{
		int iNodeIndex = rScene.nodes[i];
		const tinygltf::Node& rNode = rGltfModel.nodes[iNodeIndex];
		Parent parent {nullptr, XMMatrixIdentity(), -1};
		LoadVertices(&parent, iNodeIndex, rNode, rGltfModel, vertices, materials, nodeToJointMap, materialNodeInfos, materialNodeMap);
	}
	if (materials.size() > rGltfModel.materials.size())
	{
		Log("  Split {} materials into {} to handle primitives from different mesh nodes", rGltfModel.materials.size(), materials.size());
	}

	// Compute relative transforms for non-skinned materials and set jointCount
	std::vector<common::MaterialInfo> materialInfos(materials.size());

	// Build parent map for node hierarchy traversal (used for both skeletal and node-based)
	std::unordered_map<int, int> nodeParentMap = BuildNodeParentMap(rGltfModel);

	// Get skin joint count for skinned materials (always use skin's joint count, not animation node count)
	uint8_t uiSkinJointCount = 0;
	if (!rGltfModel.skins.empty())
	{
		size_t uiJointCount = rGltfModel.skins[0].joints.size();
		if (uiJointCount > common::kiMaxJointsPerMesh)
		{
			Log("WARNING: Model has {} joints, exceeding shader limit of {}. Skinning will use first {} joints only.",
				uiJointCount, common::kiMaxJointsPerMesh, common::kiMaxJointsPerMesh);
		}
		uiSkinJointCount = static_cast<uint8_t>(uiJointCount);
	}

	for (int64_t i = 0; i < static_cast<int64_t>(materialNodeInfos.size()); ++i)
	{
		MaterialNodeInfo& rInfo = materialNodeInfos.at(i);

		// Set jointCount: skinned materials use skin's joint count, non-skinned have 0
		materialInfos.at(i).uiJointCount = rInfo.bHasSkinning ? uiSkinJointCount : 0;

		// Store original material index for split materials (-1 means not split, same as original index)
		materialInfos.at(i).iOriginalMaterialIndex = static_cast<int16_t>(rInfo.iOriginalMaterialIndex);

		if (rInfo.bHasSkinning)
		{
			// Skinned material: use mesh node directly for mesh world matrix computation
			// At runtime: meshWorld = identity * worldMatrices[meshNodeIndex]
			if (rInfo.iNodeIndex >= 0)
			{
				materialInfos.at(i).iParentNodeIndex = static_cast<int16_t>(rInfo.iNodeIndex);
			}
			else
			{
				// Fallback: use node 0 (typically skeleton root) when mesh node is missing
				materialInfos.at(i).iParentNodeIndex = 0;
			}
			XMStoreFloat4x4(&materialInfos.at(i).f4x4RelativeTransform, XMMatrixIdentity());
			Log("  Material {}: skinned, mesh node {}", i, materialInfos.at(i).iParentNodeIndex);
		}
		else if (!rInfo.bHasSkinning && rInfo.iNodeIndex >= 0)
		{
			// Find nearest ancestor joint for this non-skinned material
			// Need to traverse node hierarchy to find joint ancestor
			int iCurrentNode = rInfo.iNodeIndex;
			int iAncestorJoint = -1;
			int iAncestorNodeIndex = -1;
			XMMATRIX matAncestorWorld = XMMatrixIdentity();

			// Traverse up to find ancestor joint
			while (iCurrentNode >= 0)
			{
				auto jointIt = nodeToJointMap.find(iCurrentNode);
				if (jointIt != nodeToJointMap.end())
				{
					// Found an ancestor that is part of the skeleton
					// nodeToJointMap uses identity mapping, so iCurrentNode is the node index we need
					iAncestorJoint = jointIt->second;
					iAncestorNodeIndex = iCurrentNode;
					matAncestorWorld = ComputeNodeWorldTransform(iCurrentNode, rGltfModel, nodeParentMap);
					break;
				}
				auto parentIt = nodeParentMap.find(iCurrentNode);
				iCurrentNode = (parentIt != nodeParentMap.end()) ? parentIt->second : -1;
			}

			if (iAncestorJoint >= 0)
			{
				materialInfos.at(i).iParentNodeIndex = static_cast<int16_t>(iAncestorNodeIndex);
				// Compute relative transform: meshBindWorld * inverse(nodeBindWorld)
				// In row-major: v * relativeTransform * nodeAnimated = v_animated
				XMMATRIX matRelative = rInfo.matMeshWorld * XMMatrixInverse(nullptr, matAncestorWorld);
				XMStoreFloat4x4(&materialInfos.at(i).f4x4RelativeTransform, matRelative);
				Log("  Material {}: non-skinned, parent node {}, mesh node {}", i, iAncestorNodeIndex, rInfo.iNodeIndex);
			}
		}
	}

	for (int64_t i = 0; i < static_cast<int64_t>(materials.size()); ++i)
	{
		// Use original material index for split materials
		int iOrigMat = materialNodeInfos.at(i).iOriginalMaterialIndex >= 0 ? materialNodeInfos.at(i).iOriginalMaterialIndex : static_cast<int>(i);
		tinygltf::Material& tinygltfMaterial = rGltfModel.materials[iOrigMat];
		Log("  {}: \"{}\"{}; {} {} {} {} {} textures, {} indices{}", i, tinygltfMaterial.name, (iOrigMat != i ? std::format(" (split from {})", iOrigMat) : ""), tinygltfMaterial.pbrMetallicRoughness.baseColorTexture.index, tinygltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index, tinygltfMaterial.normalTexture.index, tinygltfMaterial.occlusionTexture.index, tinygltfMaterial.emissiveTexture.index, materials.at(i).indexBuffer.size(), materialInfos.at(i).uiJointCount > 0 ? " (skinned)" : "");
	}

	Log("Total vertices: {}", vertices.size());

	std::unordered_map<float, int64_t> jointsMap;
	XMFLOAT3 f3Min = vertices.at(0).f3Pos;
	XMFLOAT3 f3Max = vertices.at(0).f3Pos;
	for (common::ModelVertex& rVertex : vertices)
	{
		f3Min.x = std::min(f3Min.x, rVertex.f3Pos.x);
		f3Min.y = std::min(f3Min.y, rVertex.f3Pos.y);
		f3Min.z = std::min(f3Min.z, rVertex.f3Pos.z);
		f3Max.x = std::max(f3Max.x, rVertex.f3Pos.x);
		f3Max.y = std::max(f3Max.y, rVertex.f3Pos.y);
		f3Max.z = std::max(f3Max.z, rVertex.f3Pos.z);

		++jointsMap[rVertex.fJoint];
	}
	Log("f3Min: {} f3Max: {}", f3Min, f3Max);

	Log("Joints:");
	for (const auto& [rFJointId, rICount] : jointsMap)
	{
		Log("  {}: {}", rFJointId, rICount);
	}

	std::vector<uint32_t> indices32;
	std::vector<uint32_t> materialIndexPositions(materials.size());
	for (int64_t i = 0; i < static_cast<int64_t>(materials.size()); ++i)
	{
		materialIndexPositions.at(i) = static_cast<uint32_t>(indices32.size());
		indices32.insert(indices32.end(), materials.at(i).indexBuffer.begin(), materials.at(i).indexBuffer.end());
	}

	std::vector<uint16_t> indices16;
	if (vertices.size() < std::numeric_limits<uint16_t>::max())
	{
		indices16.reserve(indices32.size());
		for (uint32_t uiIndex : indices32)
		{
			indices16.push_back(static_cast<uint16_t>(uiIndex));
		}
	}

	{
		std::filesystem::remove(path);
		std::fstream fileStreamOut(path, std::ios::out | std::ios::binary);
		size_t uiMaterialCount = materials.size();
		size_t uiIndexCount = indices32.size();
		size_t uiVertexCount = vertices.size();
		fileStreamOut.write(reinterpret_cast<const char*>(&uiMaterialCount), sizeof(uiMaterialCount));
		fileStreamOut.write(reinterpret_cast<const char*>(materialIndexPositions.data()), common::VectorByteSize(materialIndexPositions));
		fileStreamOut.write(reinterpret_cast<const char*>(materialInfos.data()), common::VectorByteSize(materialInfos));
		fileStreamOut.write(reinterpret_cast<const char*>(&uiIndexCount), sizeof(uiIndexCount));
		fileStreamOut.write(reinterpret_cast<const char*>(&uiVertexCount), sizeof(uiVertexCount));
		if (indices16.size() > 0)
		{
			fileStreamOut.write(reinterpret_cast<const char*>(indices16.data()), common::VectorByteSize(indices16));
		}
		else
		{
			fileStreamOut.write(reinterpret_cast<const char*>(indices32.data()), common::VectorByteSize(indices32));
		}
		fileStreamOut.write(reinterpret_cast<const char*>(vertices.data()), common::VectorByteSize(vertices));
		fileStreamOut.flush();
		fileStreamOut.close();
		mIntermediateFiles.push_back(path);
	}

	{
		std::filesystem::path preExportPath = GetPreExportMarkerPath();
		std::fstream fileStreamOut(preExportPath, std::ios::out | std::ios::binary);
		int64_t iVersion = GetVersion();
		fileStreamOut.write(reinterpret_cast<const char*>(&iVersion), sizeof(iVersion));
		fileStreamOut.flush();
		fileStreamOut.close();
		mIntermediateFiles.push_back(preExportPath);
	}

	Log("Samplers: {}", rGltfModel.samplers.size());
	for (const tinygltf::Sampler& rSampler : rGltfModel.samplers)
	{
		Log("  {} {} {} {}", ToVkFilter(rSampler.minFilter), ToVkFilter(rSampler.magFilter), ToVkSamplerAddressMode(rSampler.wrapS), ToVkSamplerAddressMode(rSampler.wrapT));
		ASSERT(ToVkSamplerAddressMode(rSampler.wrapS) == VK_SAMPLER_ADDRESS_MODE_REPEAT);
	}
}

void ExportScene::MainExport(tinygltf::Model& rGltfModel)
{
	// Read material count from .MODEL file first (may be larger than gltfModel.materials.size() due to splitting)
	std::filesystem::path modelPath(mInputPath);
	modelPath += ".MODEL";
	size_t uiMaterialCount = 0;
	std::fstream materialCountFileStream(modelPath, std::ios::in | std::ios::binary);
	materialCountFileStream.read(reinterpret_cast<char*>(&uiMaterialCount), sizeof(uiMaterialCount));
	materialCountFileStream.close();

	// Scene chunk data layout: [textureCrcs ALIGN16] [indexStarts ALIGN16] [MaterialShaderData]
	int64_t iTextureArraySize = common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(rGltfModel.textures.size()) * static_cast<int64_t>(sizeof(common::crc_t)));
	int64_t iIndexStartsSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(uiMaterialCount) * static_cast<int64_t>(sizeof(uint32_t)));
	int64_t iSceneArraysSize = iTextureArraySize + iIndexStartsSize;
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iSceneArraysSize + uiMaterialCount * sizeof(common::MaterialShaderData));
	common::crc_t* pTextureCrcs = reinterpret_cast<common::crc_t*>(dataSpan.data());
	uint32_t* puiIndexStarts = reinterpret_cast<uint32_t*>(dataSpan.data() + iTextureArraySize);
	common::MaterialShaderData* pMaterialShaderDatas = reinterpret_cast<common::MaterialShaderData*>(dataSpan.data() + iSceneArraysSize);

	Log("Textures: {}", rGltfModel.textures.size());
	pHeader->sceneHeader.uiTextureCount = 0;
	for (const tinygltf::Texture& rTexture : rGltfModel.textures)
	{
		bool bOcclusion = false;
		for (const tinygltf::Material& rMaterial : rGltfModel.materials)
		{
			if (IsOcclusion(rTexture.source, rMaterial))
			{
				bOcclusion = true;
				break;
			}
		}

		std::filesystem::path relativeFile = mRelativeDirectory;
		relativeFile /= mInputPath.filename();
		relativeFile += ".Texture";
		relativeFile += std::to_string(rTexture.source);
		relativeFile += bOcclusion ? ".BC4_UNORM_BLOCK" : ".BC7_UNORM_BLOCK";
		pTextureCrcs[pHeader->sceneHeader.uiTextureCount++] = common::Crc(relativeFile.string());
	}

	Log("Materials: {} (original), {} (after splitting)", rGltfModel.materials.size(), uiMaterialCount);

	// Compute and store the model CRC in the header
	pHeader->sceneHeader.modelCrc = common::Crc(mRelativeFile + ".MODEL");

	std::fstream fileStream(modelPath, std::ios::in | std::ios::binary);
	size_t uiMaterialCountVerify = 0;
	fileStream.read(reinterpret_cast<char*>(&uiMaterialCountVerify), sizeof(uiMaterialCountVerify));
	ASSERT(uiMaterialCountVerify == uiMaterialCount);
	pHeader->sceneHeader.uiMaterialCount = static_cast<uint32_t>(uiMaterialCount);
	ASSERT(pHeader->sceneHeader.uiMaterialCount <= common::SceneHeader::kiMaxMaterials);
	fileStream.read(reinterpret_cast<char*>(puiIndexStarts), uiMaterialCount * sizeof(uint32_t));

	// Read per-material skinning info for animation header
	std::vector<common::MaterialInfo> materialInfos(uiMaterialCount);
	fileStream.read(reinterpret_cast<char*>(materialInfos.data()), uiMaterialCount * sizeof(common::MaterialInfo));

	fileStream.close();

	for (size_t iMaterialIndex = 0; iMaterialIndex < uiMaterialCount; ++iMaterialIndex)
	{
		// Use original material index for split materials
		int iOrigMat = materialInfos.at(iMaterialIndex).iOriginalMaterialIndex >= 0 ? materialInfos.at(iMaterialIndex).iOriginalMaterialIndex : static_cast<int>(iMaterialIndex);
		const tinygltf::Material& rMaterial = rGltfModel.materials[iOrigMat];
		Log("  {}: {}{}", iMaterialIndex, rMaterial.name, (iOrigMat != static_cast<int>(iMaterialIndex) ? std::format(" (split from {})", iOrigMat) : ""));
		if (rMaterial.doubleSided == true)
		{
			Log("  Warning! Material is double sided");
		}

		common::MaterialShaderData materialShaderData {};

		materialShaderData.f4EmissiveFactor = XMFLOAT4(static_cast<float>(rMaterial.emissiveFactor[0]), static_cast<float>(rMaterial.emissiveFactor[1]), static_cast<float>(rMaterial.emissiveFactor[2]), 1.0f);

		// Only metallic-roughness workflow is supported
		ASSERT(rMaterial.extensions.find("KHR_materials_pbrSpecularGlossiness") == rMaterial.extensions.end());

		if (rMaterial.values.find("baseColorFactor") != rMaterial.values.end())
		{
			const double* pfData = rMaterial.values.at("baseColorFactor").ColorFactor().data();
			materialShaderData.f4BaseColorFactor = XMFLOAT4(static_cast<float>(pfData[0]), static_cast<float>(pfData[1]), static_cast<float>(pfData[2]), static_cast<float>(pfData[3]));
		}

		if (rMaterial.values.find("baseColorTexture") != rMaterial.values.end())
		{
			materialShaderData.uiColorTextureIndex = static_cast<uint8_t>(rMaterial.values.at("baseColorTexture").TextureIndex());
			Log("  baseColorTexture: {}", materialShaderData.uiColorTextureIndex);
			materialShaderData.iColorTextureSet = rMaterial.values.at("baseColorTexture").TextureTexCoord();
		}

		if (rMaterial.values.find("metallicRoughnessTexture") != rMaterial.values.end())
		{
			materialShaderData.uiPhysicalDescriptorTextureIndex = static_cast<uint8_t>(rMaterial.values.at("metallicRoughnessTexture").TextureIndex());
			Log("  metallicRoughnessTexture: {}", materialShaderData.uiPhysicalDescriptorTextureIndex);
			materialShaderData.iPhysicalDescriptorTextureSet = rMaterial.values.at("metallicRoughnessTexture").TextureTexCoord();
		}

		if (rMaterial.values.find("metallicFactor") != rMaterial.values.end())
		{
			materialShaderData.fMetallicFactor = static_cast<float>(rMaterial.values.at("metallicFactor").Factor());
		}

		if (rMaterial.values.find("roughnessFactor") != rMaterial.values.end())
		{
			materialShaderData.fRoughnessFactor = static_cast<float>(rMaterial.values.at("roughnessFactor").Factor());
		}

		// Common texture extraction
		if (rMaterial.additionalValues.find("normalTexture") != rMaterial.additionalValues.end())
		{
			materialShaderData.uiNormalTextureIndex = static_cast<uint8_t>(rMaterial.additionalValues.at("normalTexture").TextureIndex());
			Log("  normalTexture: {}", materialShaderData.uiNormalTextureIndex);
			materialShaderData.iNormalTextureSet = rMaterial.additionalValues.at("normalTexture").TextureTexCoord();
		}

		if (rMaterial.additionalValues.find("occlusionTexture") != rMaterial.additionalValues.end())
		{
			materialShaderData.uiOcclusionTextureIndex = static_cast<uint8_t>(rMaterial.additionalValues.at("occlusionTexture").TextureIndex());
			Log("  occlusionTexture: {}", materialShaderData.uiOcclusionTextureIndex);
			materialShaderData.iOcclusionTextureSet = rMaterial.additionalValues.at("occlusionTexture").TextureTexCoord();
		}

		if (rMaterial.additionalValues.find("emissiveTexture") != rMaterial.additionalValues.end())
		{
			materialShaderData.uiEmissiveTextureIndex = static_cast<uint8_t>(rMaterial.additionalValues.at("emissiveTexture").TextureIndex());
			Log("  emissiveTexture: {}", materialShaderData.uiEmissiveTextureIndex);
			materialShaderData.iEmissiveTextureSet = rMaterial.additionalValues.at("emissiveTexture").TextureTexCoord();
		}

		// Mark transparent materials with fAlphaMask >= 2.0 for runtime two-pass rendering
		if (rMaterial.alphaMode == "BLEND")
		{
			materialShaderData.fAlphaMask = 2.0f;
		}
		else
		{
			materialShaderData.fAlphaMask = 0.0f;
		}
		materialShaderData.fAlphaMaskCutoff = static_cast<float>(rMaterial.alphaCutoff);

		*(pMaterialShaderDatas++) = materialShaderData;
	}

	// Check if model has animations (skeletal or node-based)
	if (rGltfModel.animations.size() > 0)
	{
		// Log animation overview
		Log("Animation export: {} skins, {} animations", rGltfModel.skins.size(), rGltfModel.animations.size());
		if (rGltfModel.skins.size() > 0)
		{
			Log("  Skin 0 has {} joints", rGltfModel.skins[0].joints.size());
		}
		int iTotalChannels = 0;
		for (const tinygltf::Animation& rAnim : rGltfModel.animations)
		{
			iTotalChannels += static_cast<int>(rAnim.channels.size());
		}
		Log("  Total animation channels in glTF: {}", iTotalChannels);

		pHeader->sceneHeader.bHasAnimation = true;
		SkeletonData skeletonData;
		std::unordered_map<int, int> nodeToJointMap;

		bool bUseSkeletalAnimation = DetermineAnimationPath(rGltfModel);

		Log("  Using {} animation path", bUseSkeletalAnimation ? "SKELETAL" : "NODE-BASED");

		if (bUseSkeletalAnimation)
		{
			// Skeletal animation path
			Log("Loading skeletal animation...");
			skeletonData = LoadSkeleton(rGltfModel, 0);

			// Build nodeToNodeIndexMap - identity mapping since we store all nodes
			for (int64_t i = 0; i < static_cast<int64_t>(rGltfModel.nodes.size()); ++i)
			{
				nodeToJointMap[static_cast<int>(i)] = static_cast<int>(i);
			}
		}
		else
		{
			// Node-based animation path (no skin, or skin joints don't match animated nodes)
			Log("Loading node-based animation...");
			skeletonData = BuildNodeSkeleton(rGltfModel, nodeToJointMap);
		}

		Log("  {} nodes in skeleton, {} skin joints", skeletonData.skeleton.uiNodeCount, skeletonData.skeleton.uiSkinJointCount);

		// Load animations
		std::vector<common::AnimationClip> animations;
		std::vector<common::AnimationChannel> channels;
		std::vector<common::AnimationKeyframe> keyframes;
		std::vector<common::AnimationKeyframeCubic> cubicKeyframes;
		LoadAnimations(rGltfModel, nodeToJointMap, animations, channels, keyframes, cubicKeyframes);
		Log("  {} animations, {} channels, {} keyframes, {} cubic keyframes", animations.size(), channels.size(), keyframes.size(), cubicKeyframes.size());

		// Warn if all animation channels were filtered out
		if (animations.empty() && rGltfModel.animations.size() > 0)
		{
			Log("WARNING: All animation channels were filtered out!");
			Log("  nodeToJointMap size: {}", nodeToJointMap.size());
			// Log first few entries of nodeToJointMap
			int iCount = 0;
			for (const auto& [iNode, iJoint] : nodeToJointMap)
			{
				Log("    nodeToJointMap[{}] (\"{}\") = {}", iNode, rGltfModel.nodes[iNode].name, iJoint);
				if (++iCount >= 10)
				{
					break;
				}
			}
			// Log first few animation channel targets
			iCount = 0;
			for (const tinygltf::Animation& rAnim : rGltfModel.animations)
			{
				for (const tinygltf::AnimationChannel& rChannel : rAnim.channels)
				{
					Log("    Animation channel targets node {} (\"{}\")", rChannel.target_node, rChannel.target_node >= 0 ? rGltfModel.nodes[rChannel.target_node].name : "invalid");
					if (++iCount >= 10)
					{
						break;
					}
				}
				if (iCount >= 10)
				{
					break;
				}
			}
		}

		for (const common::AnimationClip& rAnim : animations)
		{
			Log("    \"{}\": {} channels, {:.2f}s duration", rAnim.pcName, rAnim.uiChannelCount, rAnim.fDuration);
		}

		// Build animation header (now small - just counts + skeleton counts)
		common::AnimationHeader animHeader {};
		animHeader.uiAnimationCount = static_cast<uint32_t>(animations.size());
		animHeader.uiChannelCount = static_cast<uint32_t>(channels.size());
		animHeader.uiKeyframeCount = static_cast<uint32_t>(keyframes.size());
		animHeader.uiCubicKeyframeCount = static_cast<uint32_t>(cubicKeyframes.size());
		animHeader.uiMaterialCount = static_cast<uint32_t>(materialInfos.size());
		animHeader.skeleton = skeletonData.skeleton;
		ASSERT(animations.size() <= common::AnimationHeader::kiMaxAnimations);

		for (int64_t i = 0; i < static_cast<int64_t>(materialInfos.size()); ++i)
		{
			if (materialInfos.at(i).iParentNodeIndex >= 0)
			{
				Log("  Material {}: non-skinned, parent node {}", i, materialInfos.at(i).iParentNodeIndex);
			}
		}

		// Compute animation data size with variable-length arrays
		int64_t iSkinJointToNodeSize = common::RoundUp<int64_t, 4>(static_cast<int64_t>(skeletonData.skinJointToNode.size()) * static_cast<int64_t>(sizeof(uint16_t)));
		int64_t iAnimDataSize = sizeof(common::AnimationHeader)
			+ skeletonData.nodes.size() * sizeof(common::ModelNode)
			+ iSkinJointToNodeSize
			+ skeletonData.inverseBindMatrices.size() * sizeof(XMFLOAT4X4)
			+ animations.size() * sizeof(common::AnimationClip)
			+ materialInfos.size() * sizeof(common::MaterialInfo)
			+ channels.size() * sizeof(common::AnimationChannel)
			+ keyframes.size() * sizeof(common::AnimationKeyframe)
			+ cubicKeyframes.size() * sizeof(common::AnimationKeyframeCubic);

		int64_t iCurrentSize = static_cast<int64_t>(mHeaderAndData.size());
		int64_t iExpectedOffset = common::kiChunkDataOffset + iSceneArraysSize + static_cast<int64_t>(uiMaterialCount) * sizeof(common::MaterialShaderData);
		Log("  Animation data: writing at offset {} (buffer size {}), expected runtime offset {} (diff={})",
			iCurrentSize, mHeaderAndData.size(), iExpectedOffset, iCurrentSize - iExpectedOffset);
		mHeaderAndData.resize(iCurrentSize + iAnimDataSize);
		std::byte* pAnimData = mHeaderAndData.data() + iCurrentSize;

		std::memcpy(pAnimData, &animHeader, sizeof(animHeader));
		pAnimData += sizeof(animHeader);

		std::memcpy(pAnimData, skeletonData.nodes.data(), skeletonData.nodes.size() * sizeof(common::ModelNode));
		pAnimData += skeletonData.nodes.size() * sizeof(common::ModelNode);

		if (!skeletonData.skinJointToNode.empty())
		{
			std::memcpy(pAnimData, skeletonData.skinJointToNode.data(), skeletonData.skinJointToNode.size() * sizeof(uint16_t));
		}
		pAnimData += iSkinJointToNodeSize;

		if (!skeletonData.inverseBindMatrices.empty())
		{
			std::memcpy(pAnimData, skeletonData.inverseBindMatrices.data(), skeletonData.inverseBindMatrices.size() * sizeof(XMFLOAT4X4));
		}
		pAnimData += skeletonData.inverseBindMatrices.size() * sizeof(XMFLOAT4X4);

		std::memcpy(pAnimData, animations.data(), animations.size() * sizeof(common::AnimationClip));
		pAnimData += animations.size() * sizeof(common::AnimationClip);

		std::memcpy(pAnimData, materialInfos.data(), materialInfos.size() * sizeof(common::MaterialInfo));
		pAnimData += materialInfos.size() * sizeof(common::MaterialInfo);

		std::memcpy(pAnimData, channels.data(), channels.size() * sizeof(common::AnimationChannel));
		pAnimData += channels.size() * sizeof(common::AnimationChannel);

		std::memcpy(pAnimData, keyframes.data(), keyframes.size() * sizeof(common::AnimationKeyframe));
		pAnimData += keyframes.size() * sizeof(common::AnimationKeyframe);

		std::memcpy(pAnimData, cubicKeyframes.data(), cubicKeyframes.size() * sizeof(common::AnimationKeyframeCubic));
	}
}

void ExportScene::CleanupOnFailure()
{
	for (const std::filesystem::path& rPath : mIntermediateFiles)
	{
		std::filesystem::remove(rPath);
	}
	mIntermediateFiles.clear();
}
