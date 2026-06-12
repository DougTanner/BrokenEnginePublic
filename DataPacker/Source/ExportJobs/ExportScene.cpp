#include "ExportScene.h"

#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#endif
#include "tinygltf/tiny_gltf.h"
#ifdef __clang__
	#pragma clang diagnostic pop
#endif
#pragma warning(pop)

#include "Scene/SceneAnimationLoader.h"
#include "Texture/Texture.h"

#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#endif
#include "meshoptimizer.h"
#ifdef __clang__
	#pragma clang diagnostic pop
#endif
#pragma warning(pop)

using enum common::ChunkFlags;

std::optional<common::ChunkFlags_t> ExportScene::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	if (rDirectoryEntry.path().extension() != ".gltf")
	{
		return std::nullopt;
	}
	// Skip glTF files inside any Intermediates/ directory: those are bake artifacts (e.g., the
	// Gaea Mesher's Mesh.gltf alongside Mesh.bin) consumed by ExportIsland, not standalone scenes.
	for (const std::filesystem::path& rPart : rDirectoryEntry.path())
	{
		if (rPart == "Intermediates")
		{
			return std::nullopt;
		}
	}
	return std::optional<common::ChunkFlags_t>(common::ChunkFlags::kScene);
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
			LOG(kDefault, kDebug, "Removing pre-export marker due to dirty main file");
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
			if (!fileStreamIn || iStoredVersion != GetVersion())
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
			ASSERT(false);
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
			ASSERT(false);
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

std::vector<VkFormat> ComputeTextureFormats(const tinygltf::Model& rModel)
{
	std::vector<VkFormat> textureFormats;
	textureFormats.reserve(rModel.textures.size());
	for (const tinygltf::Texture& rTexture : rModel.textures)
	{
		VkFormat vkFormat = VK_FORMAT_BC7_UNORM_BLOCK;
		for (const tinygltf::Material& rMaterial : rModel.materials)
		{
			if (IsOcclusion(rTexture.source, rMaterial))
			{
				vkFormat = VK_FORMAT_BC4_UNORM_BLOCK;
				break;
			}
			if (IsNormal(rTexture.source, rMaterial))
			{
				vkFormat = VK_FORMAT_BC5_UNORM_BLOCK;
				break;
			}
		}
		textureFormats.push_back(vkFormat);
	}
	return textureFormats;
}

std::filesystem::path ExportScene::GetPreExportMarkerPath() const
{
	std::filesystem::path path(mInputPath);
	path += ".PreExport";
	return path;
}

std::filesystem::path ExportScene::GetTextureIntermediatePath(int64_t iTextureIndex, VkFormat vkFormat) const
{
	std::filesystem::path path(mInputPath);
	path += ".Texture";
	path += std::to_string(iTextureIndex);
	switch (vkFormat)
	{
		case VK_FORMAT_BC4_UNORM_BLOCK:
			path += ".BC4_UNORM_BLOCK";
			break;
		case VK_FORMAT_BC5_UNORM_BLOCK:
			path += ".BC5_UNORM_BLOCK";
			break;
		case VK_FORMAT_BC7_UNORM_BLOCK:
			path += ".BC7_UNORM_BLOCK";
			break;
		default:
			DEBUG_BREAK();
			break;
	}
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
	tinygltf::TinyGLTF gltfContext;
	bool bFileLoaded = bBinary ? gltfContext.LoadBinaryFromFile(&gltfModel, &error, &warning, filename.c_str()) : gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename.c_str());
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
		bNeedsPreExport = (!fileStreamIn || iStoredVersion != GetVersion());
	}

	if (bNeedsPreExport)
	{
		PreExport(gltfModel);
	}

	MainExport(gltfModel);
}

void ExportScene::PreExport(tinygltf::Model& rGltfModel)
{
	LOG(kDefault, kDebug, "PreExport Gltf: {}", mInputPath.string());

	ProcessTextures(rGltfModel);

	LOG(kDefault, kDebug, "Loading {} materials", rGltfModel.materials.size());
	std::vector<Material> materials(rGltfModel.materials.size());
	std::vector<MaterialNodeInfo> materialNodeInfos(rGltfModel.materials.size());
	std::unordered_map<int, int> nodeToJointMap;
	SetupSkeletonAndMaterials(rGltfModel, nodeToJointMap);

	std::vector<common::ModelVertex> vertices;
	LoadVerticesAndOptimizeMeshes(rGltfModel, nodeToJointMap, materials, materialNodeInfos, vertices);

	std::vector<common::MaterialInfo> materialInfos(materials.size());
	BuildMaterialInfos(rGltfModel, nodeToJointMap, materials, materialNodeInfos, materialInfos);

	WriteModelFile(materials, materialInfos, vertices);

	LOG(kDefault, kDebug, "Samplers: {}", rGltfModel.samplers.size());
	for (const tinygltf::Sampler& rSampler : rGltfModel.samplers)
	{
		LOG(kDefault, kVerbose, "  {} {} {} {}", ToVkFilter(rSampler.minFilter), ToVkFilter(rSampler.magFilter), ToVkSamplerAddressMode(rSampler.wrapS), ToVkSamplerAddressMode(rSampler.wrapT));
		ASSERT(ToVkSamplerAddressMode(rSampler.wrapS) == VK_SAMPLER_ADDRESS_MODE_REPEAT);
	}
}

void ExportScene::ProcessTextures(tinygltf::Model& rGltfModel)
{
	ASSERT(rGltfModel.textures.size() <= common::SceneHeader::kiMaxTextures);
	LOG(kDefault, kDebug, "Pre-processing {} textures", rGltfModel.textures.size());

	std::vector<VkFormat> textureFormats = ComputeTextureFormats(rGltfModel);

	// Launch async texture processing tasks
	std::vector<std::future<void>> futures;
	futures.reserve(rGltfModel.textures.size());
	for (size_t i = 0; i < rGltfModel.textures.size(); ++i)
	{
		VkFormat vkFormat = textureFormats.at(i);
		const tinygltf::Image& rImage = rGltfModel.images.at(rGltfModel.textures.at(i).source);
		std::filesystem::path path = GetTextureIntermediatePath(rGltfModel.textures.at(i).source, vkFormat);

		futures.push_back(std::async(std::launch::async, [vkFormat, &rImage, path]()
		{
			std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
			Texture texture(reinterpret_cast<const std::byte*>(rImage.image.data()), rImage.width, rImage.height, rImage.component);
			texture.MakeMipmaps(vkFormat);
			texture.Save(path, vkFormat, {});
		}));
	}

	// Wait for all tasks and log results
	int64_t iTextureIndex = 0;
	for (size_t i = 0; i < rGltfModel.textures.size(); ++i)
	{
		futures.at(i).get();
		const tinygltf::Image& rImage = rGltfModel.images.at(rGltfModel.textures.at(i).source);
		std::filesystem::path path = GetTextureIntermediatePath(rGltfModel.textures.at(i).source, textureFormats.at(i));
		mIntermediateFiles.push_back(path);
		LOG(kDefault, kVerbose, "  {}: Texture {} -> {}", iTextureIndex++, rImage.uri, path.filename().native());
	}
}

void ExportScene::SetupSkeletonAndMaterials(tinygltf::Model& rGltfModel, std::unordered_map<int, int>& rNodeToJointMap)
{
	bool bUseSkeletalAnimation = DetermineAnimationPath(rGltfModel);

	if (bUseSkeletalAnimation)
	{
		LoadSkeletonData(rGltfModel, rNodeToJointMap);
		LOG(kDefault, kDebug, "  Skeletal animation detected: {} skin joints, {} total nodes", rGltfModel.skins.at(0).joints.size(), rGltfModel.nodes.size());
	}
	else if (rGltfModel.animations.size() > 0)
	{
		SkeletonData tempSkeletonData = LoadSkeletonData(rGltfModel, rNodeToJointMap);
		LOG(kDefault, kDebug, "  Node-based animation detected: {} nodes in skeleton", tempSkeletonData.skeleton.uiNodeCount);
	}
}

void ExportScene::LoadVerticesAndOptimizeMeshes(tinygltf::Model& rGltfModel, const std::unordered_map<int, int>& rNodeToJointMap, std::vector<Material>& rMaterials, std::vector<MaterialNodeInfo>& rMaterialNodeInfos, std::vector<common::ModelVertex>& rVertices)
{
	const tinygltf::Scene& rScene = rGltfModel.scenes.at(rGltfModel.defaultScene > -1 ? rGltfModel.defaultScene : 0);
	std::unordered_map<std::pair<int, int>, int, PairHash> materialNodeMap;
	LoadVerticesContext loadContext {.rVertices = rVertices, .rMaterials = rMaterials, .rMaterialNodeInfos = rMaterialNodeInfos, .rMaterialNodeMap = materialNodeMap, .rNodeToJointMap = rNodeToJointMap};
	for (size_t i = 0; i < rScene.nodes.size(); ++i)
	{
		int iNodeIndex = rScene.nodes.at(i);
		const tinygltf::Node& rNode = rGltfModel.nodes.at(iNodeIndex);
		Parent parent {nullptr, XMMatrixIdentity(), -1};
		LoadVertices(&parent, iNodeIndex, rNode, rGltfModel, loadContext);
	}
	if (rMaterials.size() > rGltfModel.materials.size())
	{
		LOG(kDefault, kDebug, "  Split {} materials into {} to handle primitives from different mesh nodes", rGltfModel.materials.size(), rMaterials.size());
	}

	// Optimize triangle order per material for GPU vertex cache and overdraw
	for (int64_t i = 0; i < static_cast<int64_t>(rMaterials.size()); ++i)
	{
		std::vector<uint32_t>& rIndexBuffer = rMaterials.at(i).indexBuffer;
		if (rIndexBuffer.empty())
		{
			continue;
		}
		meshopt_optimizeVertexCache(rIndexBuffer.data(), rIndexBuffer.data(), rIndexBuffer.size(), rVertices.size());
		meshopt_optimizeOverdraw(rIndexBuffer.data(), rIndexBuffer.data(), rIndexBuffer.size(), &rVertices.at(0).f3Pos.x, rVertices.size(), sizeof(common::ModelVertex), 1.05f);
		LOG(kDefault, kVerbose, "  Material {}: optimized {} triangles", i, rIndexBuffer.size() / 3);
	}
}

void ExportScene::BuildMaterialInfos(tinygltf::Model& rGltfModel, const std::unordered_map<int, int>& rNodeToJointMap, const std::vector<Material>& rMaterials, const std::vector<MaterialNodeInfo>& rMaterialNodeInfos, std::vector<common::MaterialInfo>& rMaterialInfos)
{
	// Build parent map for node hierarchy traversal (used for both skeletal and node-based)
	std::unordered_map<int, int> nodeParentMap = BuildNodeParentMap(rGltfModel);

	// Get skin joint count for skinned materials (always use skin's joint count, not animation node count)
	uint8_t uiSkinJointCount = 0;
	if (!rGltfModel.skins.empty())
	{
		size_t uiJointCount = rGltfModel.skins.at(0).joints.size();
		if (uiJointCount > common::kiMaxJointsPerMesh)
		{
			LOG(kDefault, kWarning, "WARNING: Model has {} joints, exceeding shader limit of {}. Skinning will use first {} joints only.", uiJointCount, common::kiMaxJointsPerMesh, common::kiMaxJointsPerMesh);
		}
		uiSkinJointCount = static_cast<uint8_t>(uiJointCount);
	}

	for (int64_t i = 0; i < static_cast<int64_t>(rMaterialNodeInfos.size()); ++i)
	{
		const MaterialNodeInfo& rInfo = rMaterialNodeInfos.at(i);

		// Set jointCount: skinned materials use skin's joint count, non-skinned have 0
		rMaterialInfos.at(i).uiJointCount = rInfo.bHasSkinning ? uiSkinJointCount : 0;

		// Store original material index for split materials (-1 means not split, same as original index)
		rMaterialInfos.at(i).iOriginalMaterialIndex = static_cast<int16_t>(rInfo.iOriginalMaterialIndex);

		if (rInfo.bHasSkinning)
		{
			// Skinned material: use mesh node directly for mesh world matrix computation
			// At runtime: meshWorld = identity * worldMatrices[meshNodeIndex]
			if (rInfo.iNodeIndex >= 0)
			{
				rMaterialInfos.at(i).iParentNodeIndex = static_cast<int16_t>(rInfo.iNodeIndex);
			}
			else
			{
				// Fallback: use node 0 (typically skeleton root) when mesh node is missing
				rMaterialInfos.at(i).iParentNodeIndex = 0;
			}
			XMStoreFloat4x4(&rMaterialInfos.at(i).f4x4RelativeTransform, XMMatrixIdentity());
			LOG(kDefault, kVerbose, "  Material {}: skinned, mesh node {}", i, rMaterialInfos.at(i).iParentNodeIndex);
		}
		else if (rInfo.iNodeIndex >= 0)
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
				auto jointIt = rNodeToJointMap.find(iCurrentNode);
				if (jointIt != rNodeToJointMap.end())
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
				rMaterialInfos.at(i).iParentNodeIndex = static_cast<int16_t>(iAncestorNodeIndex);
				// Compute relative transform: meshBindWorld * inverse(nodeBindWorld)
				// In row-major: v * relativeTransform * nodeAnimated = v_animated
				XMMATRIX matRelative = rInfo.matMeshWorld * XMMatrixInverse(nullptr, matAncestorWorld);
				XMStoreFloat4x4(&rMaterialInfos.at(i).f4x4RelativeTransform, matRelative);
				LOG(kDefault, kVerbose, "  Material {}: non-skinned, parent node {}, mesh node {}", i, iAncestorNodeIndex, rInfo.iNodeIndex);
			}
		}
	}

	for (int64_t i = 0; i < static_cast<int64_t>(rMaterials.size()); ++i)
	{
		// Use original material index for split materials
		int iOrigMat = rMaterialNodeInfos.at(i).iOriginalMaterialIndex >= 0 ? rMaterialNodeInfos.at(i).iOriginalMaterialIndex : static_cast<int>(i);
		tinygltf::Material& rTinygltfMaterial = rGltfModel.materials.at(iOrigMat);
		LOG(kDefault, kVerbose, "  {}: \"{}\"{}; {} {} {} {} {} textures, {} indices{}", i, rTinygltfMaterial.name, (iOrigMat != i ? std::format(" (split from {})", iOrigMat) : ""), rTinygltfMaterial.pbrMetallicRoughness.baseColorTexture.index, rTinygltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index, rTinygltfMaterial.normalTexture.index, rTinygltfMaterial.occlusionTexture.index, rTinygltfMaterial.emissiveTexture.index, rMaterials.at(i).indexBuffer.size(), rMaterialInfos.at(i).uiJointCount > 0 ? " (skinned)" : "");
	}
}

void ExportScene::WriteModelFile(const std::vector<Material>& rMaterials, const std::vector<common::MaterialInfo>& rMaterialInfos, std::vector<common::ModelVertex>& rVertices)
{
	std::filesystem::path path(mInputPath);
	path += ".MODEL";

	LOG(kDefault, kDebug, "Total vertices: {}", rVertices.size());

	std::unordered_map<float, int64_t> jointsMap;
	XMFLOAT3 f3Min = rVertices.at(0).f3Pos;
	XMFLOAT3 f3Max = rVertices.at(0).f3Pos;
	for (common::ModelVertex& rVertex : rVertices)
	{
		f3Min.x = std::min(f3Min.x, rVertex.f3Pos.x);
		f3Min.y = std::min(f3Min.y, rVertex.f3Pos.y);
		f3Min.z = std::min(f3Min.z, rVertex.f3Pos.z);
		f3Max.x = std::max(f3Max.x, rVertex.f3Pos.x);
		f3Max.y = std::max(f3Max.y, rVertex.f3Pos.y);
		f3Max.z = std::max(f3Max.z, rVertex.f3Pos.z);

		++jointsMap.try_emplace(rVertex.fJoint, 0).first->second;
	}
	LOG(kDefault, kDebug, "f3Min: {} f3Max: {}", f3Min, f3Max);

	LOG(kDefault, kDebug, "Joints:");
	for (const auto& [rFJointId, rICount] : jointsMap)
	{
		LOG(kDefault, kVerbose, "  {}: {}", rFJointId, rICount);
	}

	std::vector<uint32_t> indices32;
	std::vector<uint32_t> materialIndexPositions(rMaterials.size());
	for (int64_t i = 0; i < static_cast<int64_t>(rMaterials.size()); ++i)
	{
		materialIndexPositions.at(i) = static_cast<uint32_t>(indices32.size());
		indices32.insert(indices32.end(), rMaterials.at(i).indexBuffer.begin(), rMaterials.at(i).indexBuffer.end());
	}

	// Reorder vertices for sequential access and remap indices
	std::vector<common::ModelVertex> optimizedVertices(rVertices.size());
	meshopt_optimizeVertexFetch(optimizedVertices.data(), indices32.data(), indices32.size(), rVertices.data(), rVertices.size(), sizeof(common::ModelVertex));
	rVertices = std::move(optimizedVertices);
	LOG(kDefault, kDebug, "Mesh optimized: {} vertices, {} indices ({} materials)", rVertices.size(), indices32.size(), rMaterials.size());

	std::vector<uint16_t> indices16;
	if (rVertices.size() < std::numeric_limits<uint16_t>::max())
	{
		indices16.reserve(indices32.size());
		for (uint32_t uiIndex : indices32)
		{
			indices16.push_back(static_cast<uint16_t>(uiIndex));
		}
	}

	std::filesystem::remove(path);
	std::fstream fileStreamOut(path, std::ios::out | std::ios::binary);
	size_t uiMaterialCount = rMaterials.size();
	size_t uiIndexCount = indices32.size();
	size_t uiVertexCount = rVertices.size();
	fileStreamOut.write(reinterpret_cast<const char*>(&uiMaterialCount), sizeof(uiMaterialCount));
	fileStreamOut.write(reinterpret_cast<const char*>(materialIndexPositions.data()), common::VectorByteSize(materialIndexPositions));
	fileStreamOut.write(reinterpret_cast<const char*>(rMaterialInfos.data()), common::VectorByteSize(rMaterialInfos));
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
	fileStreamOut.write(reinterpret_cast<const char*>(rVertices.data()), common::VectorByteSize(rVertices));
	fileStreamOut.flush();
	fileStreamOut.close();
	mIntermediateFiles.push_back(path);

	std::filesystem::path preExportMarkerPath = GetPreExportMarkerPath();
	std::fstream fileStreamOutMarker(preExportMarkerPath, std::ios::out | std::ios::binary);
	int64_t iVersion = GetVersion();
	fileStreamOutMarker.write(reinterpret_cast<const char*>(&iVersion), sizeof(iVersion));
	fileStreamOutMarker.flush();
	fileStreamOutMarker.close();
	mIntermediateFiles.push_back(preExportMarkerPath);
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

	LOG(kDefault, kDebug, "Textures: {}", rGltfModel.textures.size());
	std::vector<VkFormat> textureFormats = ComputeTextureFormats(rGltfModel);
	pHeader->sceneHeader.uiTextureCount = 0;
	for (size_t i = 0; i < rGltfModel.textures.size(); ++i)
	{
		const tinygltf::Texture& rTexture = rGltfModel.textures.at(i);
		std::filesystem::path relativeFile = mRelativeDirectory;
		relativeFile /= mInputPath.filename();
		relativeFile += ".Texture";
		relativeFile += std::to_string(rTexture.source);
		switch (textureFormats.at(i))
		{
			case VK_FORMAT_BC4_UNORM_BLOCK:
				relativeFile += ".BC4_UNORM_BLOCK";
				break;
			case VK_FORMAT_BC5_UNORM_BLOCK:
				relativeFile += ".BC5_UNORM_BLOCK";
				break;
			case VK_FORMAT_BC7_UNORM_BLOCK:
				relativeFile += ".BC7_UNORM_BLOCK";
				break;
			default:
				DEBUG_BREAK();
				break;
		}
		pTextureCrcs[pHeader->sceneHeader.uiTextureCount++] = common::Crc(relativeFile.string());
	}

	LOG(kDefault, kDebug, "Materials: {} (original), {} (after splitting)", rGltfModel.materials.size(), uiMaterialCount);

	// Compute and store the model CRC in the header
	pHeader->sceneHeader.modelCrc = common::Crc(mRelativeFile + ".MODEL");

	std::vector<common::MaterialInfo> materialInfos(uiMaterialCount);
	ReadMaterialInfosFromModel(modelPath, uiMaterialCount, puiIndexStarts, materialInfos);
	pHeader->sceneHeader.uiMaterialCount = static_cast<uint32_t>(uiMaterialCount);
	ASSERT(pHeader->sceneHeader.uiMaterialCount <= common::SceneHeader::kiMaxMaterials);

	FillMaterialShaderDatas(rGltfModel, materialInfos, pMaterialShaderDatas);

	// Check if model has animations (skeletal or node-based)
	if (rGltfModel.animations.size() > 0)
	{
		WriteAnimationSection(rGltfModel, materialInfos, pHeader, iSceneArraysSize);
	}
}

void ExportScene::ReadMaterialInfosFromModel(const std::filesystem::path& rModelPath, size_t uiMaterialCount, uint32_t* puiIndexStarts, std::vector<common::MaterialInfo>& rMaterialInfos)
{
	std::fstream fileStream(rModelPath, std::ios::in | std::ios::binary);
	size_t uiMaterialCountVerify = 0;
	fileStream.read(reinterpret_cast<char*>(&uiMaterialCountVerify), sizeof(uiMaterialCountVerify));
	ASSERT(uiMaterialCountVerify == uiMaterialCount);
	fileStream.read(reinterpret_cast<char*>(puiIndexStarts), uiMaterialCount * sizeof(uint32_t));

	// Read per-material skinning info for animation header
	fileStream.read(reinterpret_cast<char*>(rMaterialInfos.data()), uiMaterialCount * sizeof(common::MaterialInfo));

	fileStream.close();
}

void ExportScene::FillMaterialShaderDatas(tinygltf::Model& rGltfModel, const std::vector<common::MaterialInfo>& rMaterialInfos, common::MaterialShaderData* pMaterialShaderDatas)
{
	for (size_t iMaterialIndex = 0; iMaterialIndex < rMaterialInfos.size(); ++iMaterialIndex)
	{
		// Use original material index for split materials
		int iOrigMat = rMaterialInfos.at(iMaterialIndex).iOriginalMaterialIndex >= 0 ? rMaterialInfos.at(iMaterialIndex).iOriginalMaterialIndex : static_cast<int>(iMaterialIndex);
		const tinygltf::Material& rMaterial = rGltfModel.materials.at(iOrigMat);
		LOG(kDefault, kVerbose, "  {}: {}{}", iMaterialIndex, rMaterial.name, (iOrigMat != static_cast<int>(iMaterialIndex) ? std::format(" (split from {})", iOrigMat) : ""));
		if (rMaterial.doubleSided == true)
		{
			LOG(kDefault, kWarning, "  Warning! Material is double sided");
		}

		common::MaterialShaderData materialShaderData {};

		materialShaderData.f4EmissiveFactor = XMFLOAT4(static_cast<float>(rMaterial.emissiveFactor.at(0)), static_cast<float>(rMaterial.emissiveFactor.at(1)), static_cast<float>(rMaterial.emissiveFactor.at(2)), 1.0f);

		// Only metallic-roughness workflow is supported
		ASSERT(rMaterial.extensions.find("KHR_materials_pbrSpecularGlossiness") == rMaterial.extensions.end());

		if (rMaterial.values.find("baseColorFactor") != rMaterial.values.end())
		{
			const auto& rColorFactor = rMaterial.values.at("baseColorFactor").ColorFactor();
			materialShaderData.f4BaseColorFactor = XMFLOAT4(static_cast<float>(rColorFactor[0]), static_cast<float>(rColorFactor[1]), static_cast<float>(rColorFactor[2]), static_cast<float>(rColorFactor[3]));
		}

		if (rMaterial.values.find("baseColorTexture") != rMaterial.values.end())
		{
			materialShaderData.uiColorTextureIndex = static_cast<uint8_t>(rMaterial.values.at("baseColorTexture").TextureIndex());
			LOG(kDefault, kVerbose, "  baseColorTexture: {}", materialShaderData.uiColorTextureIndex);
			materialShaderData.iColorTextureSet = rMaterial.values.at("baseColorTexture").TextureTexCoord();
		}

		if (rMaterial.values.find("metallicRoughnessTexture") != rMaterial.values.end())
		{
			materialShaderData.uiPhysicalDescriptorTextureIndex = static_cast<uint8_t>(rMaterial.values.at("metallicRoughnessTexture").TextureIndex());
			LOG(kDefault, kVerbose, "  metallicRoughnessTexture: {}", materialShaderData.uiPhysicalDescriptorTextureIndex);
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
			LOG(kDefault, kVerbose, "  normalTexture: {}", materialShaderData.uiNormalTextureIndex);
			materialShaderData.iNormalTextureSet = rMaterial.additionalValues.at("normalTexture").TextureTexCoord();
		}

		if (rMaterial.additionalValues.find("occlusionTexture") != rMaterial.additionalValues.end())
		{
			materialShaderData.uiOcclusionTextureIndex = static_cast<uint8_t>(rMaterial.additionalValues.at("occlusionTexture").TextureIndex());
			LOG(kDefault, kVerbose, "  occlusionTexture: {}", materialShaderData.uiOcclusionTextureIndex);
			materialShaderData.iOcclusionTextureSet = rMaterial.additionalValues.at("occlusionTexture").TextureTexCoord();
		}

		if (rMaterial.additionalValues.find("emissiveTexture") != rMaterial.additionalValues.end())
		{
			materialShaderData.uiEmissiveTextureIndex = static_cast<uint8_t>(rMaterial.additionalValues.at("emissiveTexture").TextureIndex());
			LOG(kDefault, kVerbose, "  emissiveTexture: {}", materialShaderData.uiEmissiveTextureIndex);
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
}

void ExportScene::WriteAnimationSection(tinygltf::Model& rGltfModel, const std::vector<common::MaterialInfo>& rMaterialInfos, common::ChunkHeader* pHeader, int64_t iSceneArraysSize)
{
	// Log animation overview
	LOG(kDefault, kDebug, "Animation export: {} skins, {} animations", rGltfModel.skins.size(), rGltfModel.animations.size());
	if (rGltfModel.skins.size() > 0)
	{
		LOG(kDefault, kDebug, "  Skin 0 has {} joints", rGltfModel.skins.at(0).joints.size());
	}
	int iTotalChannels = 0;
	for (const tinygltf::Animation& rAnim : rGltfModel.animations)
	{
		iTotalChannels += static_cast<int>(rAnim.channels.size());
	}
	LOG(kDefault, kDebug, "  Total animation channels in glTF: {}", iTotalChannels);

	pHeader->sceneHeader.bHasAnimation = true;
	SkeletonData skeletonData;
	std::unordered_map<int, int> nodeToJointMap;

	bool bUseSkeletalAnimation = DetermineAnimationPath(rGltfModel);

	LOG(kDefault, kDebug, "  Using {} animation path", bUseSkeletalAnimation ? "SKELETAL" : "NODE-BASED");

	skeletonData = LoadSkeletonData(rGltfModel, nodeToJointMap);

	LOG(kDefault, kDebug, "  {} nodes in skeleton, {} skin joints", skeletonData.skeleton.uiNodeCount, skeletonData.skeleton.uiSkinJointCount);

	// Load animations
	std::vector<common::AnimationClip> animations;
	std::vector<common::AnimationChannel> channels;
	std::vector<common::AnimationKeyframe> keyframes;
	std::vector<common::AnimationKeyframeCubic> cubicKeyframes;
	AnimationOutput animationOut {.rAnimations = animations, .rChannels = channels, .rKeyframes = keyframes, .rCubicKeyframes = cubicKeyframes};
	LoadAnimations(rGltfModel, nodeToJointMap, animationOut);
	LOG(kDefault, kDebug, "  {} animations, {} channels, {} keyframes, {} cubic keyframes", animations.size(), channels.size(), keyframes.size(), cubicKeyframes.size());

	// Warn if all animation channels were filtered out
	if (animations.empty() && rGltfModel.animations.size() > 0)
	{
		LOG(kDefault, kWarning, "WARNING: All animation channels were filtered out!");
		LOG(kDefault, kWarning, "  nodeToJointMap size: {}", nodeToJointMap.size());
		// Log first few entries of nodeToJointMap
		int iCount = 0;
		for (const auto& [iNode, iJoint] : nodeToJointMap)
		{
			LOG(kDefault, kWarning, "    nodeToJointMap[{}] (\"{}\") = {}", iNode, rGltfModel.nodes.at(iNode).name, iJoint);
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
				LOG(kDefault, kWarning, "    Animation channel targets node {} (\"{}\")", rChannel.target_node, rChannel.target_node >= 0 ? rGltfModel.nodes.at(rChannel.target_node).name : "invalid");
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
		LOG(kDefault, kVerbose, "    \"{}\": {} channels, {:.2f}s duration", rAnim.pcName, rAnim.uiChannelCount, rAnim.fDuration);
	}

	// Build animation header (now small - just counts + skeleton counts)
	common::AnimationHeader animHeader {};
	animHeader.uiAnimationCount = static_cast<uint32_t>(animations.size());
	animHeader.uiChannelCount = static_cast<uint32_t>(channels.size());
	animHeader.uiKeyframeCount = static_cast<uint32_t>(keyframes.size());
	animHeader.uiCubicKeyframeCount = static_cast<uint32_t>(cubicKeyframes.size());
	animHeader.uiMaterialCount = static_cast<uint32_t>(rMaterialInfos.size());
	animHeader.skeleton = skeletonData.skeleton;
	ASSERT(animations.size() <= common::AnimationHeader::kiMaxAnimations);

	for (int64_t i = 0; i < static_cast<int64_t>(rMaterialInfos.size()); ++i)
	{
		if (rMaterialInfos.at(i).iParentNodeIndex >= 0)
		{
			LOG(kDefault, kVerbose, "  Material {}: non-skinned, parent node {}", i, rMaterialInfos.at(i).iParentNodeIndex);
		}
	}

	// Compute animation data size with variable-length arrays
	int64_t iSkinJointToNodeSize = common::RoundUp<int64_t, 4>(static_cast<int64_t>(skeletonData.skinJointToNode.size()) * static_cast<int64_t>(sizeof(uint16_t)));
	int64_t iAnimDataSize = sizeof(common::AnimationHeader)
		+ skeletonData.nodes.size() * sizeof(common::ModelNode)
		+ iSkinJointToNodeSize
		+ skeletonData.inverseBindMatrices.size() * sizeof(XMFLOAT4X4)
		+ animations.size() * sizeof(common::AnimationClip)
		+ rMaterialInfos.size() * sizeof(common::MaterialInfo)
		+ channels.size() * sizeof(common::AnimationChannel)
		+ keyframes.size() * sizeof(common::AnimationKeyframe)
		+ cubicKeyframes.size() * sizeof(common::AnimationKeyframeCubic);

	int64_t iCurrentSize = static_cast<int64_t>(mHeaderAndData.size());
	// Mirrors the reader's math (FileManager.cpp): the MaterialShaderData block is 16-byte rounded by AllocateHeaderAndData
	int64_t iExpectedOffset = common::kiChunkDataOffset + iSceneArraysSize + common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(rMaterialInfos.size()) * static_cast<int64_t>(sizeof(common::MaterialShaderData)));
	LOG(kDefault, kVerbose, "  Animation data: writing at offset {} (buffer size {}), expected runtime offset {} (diff {})", iCurrentSize, mHeaderAndData.size(), iExpectedOffset, iCurrentSize - iExpectedOffset);
	ASSERT(iCurrentSize == iExpectedOffset);
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

	std::memcpy(pAnimData, rMaterialInfos.data(), rMaterialInfos.size() * sizeof(common::MaterialInfo));
	pAnimData += rMaterialInfos.size() * sizeof(common::MaterialInfo);

	std::memcpy(pAnimData, channels.data(), channels.size() * sizeof(common::AnimationChannel));
	pAnimData += channels.size() * sizeof(common::AnimationChannel);

	std::memcpy(pAnimData, keyframes.data(), keyframes.size() * sizeof(common::AnimationKeyframe));
	pAnimData += keyframes.size() * sizeof(common::AnimationKeyframe);

	std::memcpy(pAnimData, cubicKeyframes.data(), cubicKeyframes.size() * sizeof(common::AnimationKeyframeCubic));
}

void ExportScene::CleanupOnFailure()
{
	for (const std::filesystem::path& rPath : mIntermediateFiles)
	{
		std::filesystem::remove(rPath);
	}
	mIntermediateFiles.clear();
}
