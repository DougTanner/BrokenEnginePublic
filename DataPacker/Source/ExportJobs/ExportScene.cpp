#include "ExportScene.h"

#include "Texture.h"

using enum common::ChunkFlags;

// Hash function for std::pair<int, int> to use with std::unordered_map
struct PairHash
{
	size_t operator()(const std::pair<int, int>& rPair) const
	{
		return std::hash<int>()(rPair.first) ^ (std::hash<int>()(rPair.second) << 1);
	}
};

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
			common::DebugBreak();
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
			common::DebugBreak();
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

void GetIndexVertexCount(const tinygltf::Node& rNode, const tinygltf::Model& rModel, int64_t& riIndexCount, int64_t& riVertexCount)
{
	if (rNode.children.size() > 0)
	{
		for (int64_t i = 0; i < static_cast<int64_t>(rNode.children.size()); ++i)
		{
			GetIndexVertexCount(rModel.nodes[rNode.children[i]], rModel, riIndexCount, riVertexCount);
		}
	}

	if (rNode.mesh > -1)
	{
		const tinygltf::Mesh mesh = rModel.meshes[rNode.mesh];
		for (int64_t i = 0; i < static_cast<int64_t>(mesh.primitives.size()); ++i)
		{
			tinygltf::Primitive primitive = mesh.primitives[i];
			riVertexCount += rModel.accessors[primitive.attributes.find("POSITION")->second].count;
			if (primitive.indices > -1)
			{
				riIndexCount += rModel.accessors[primitive.indices].count;
			}
		}
	}
}

struct Node
{
	Node* pParent = nullptr;
};

struct Material
{
	std::vector<uint32_t> indexBuffer;
};

struct Parent
{
	Parent* pParent = nullptr;
	XMMATRIX matNode {};
	int iNodeIndex = -1;
};

// Tracks per-material skinning metadata during export
struct MaterialNodeInfo
{
	bool bHasSkinning = false;  // True if any primitive has JOINTS_0 attribute
	int iNodeIndex = -1;        // Node index of the mesh contributing to this material
	XMMATRIX matMeshWorld = XMMatrixIdentity();  // World transform of mesh at bind pose (accumulated matLocal)
	int iOriginalMaterialIndex = -1;  // Original glTF material index (for split materials)
};

struct AncestorJointResult
{
	int iJointIndex = 0;
	XMMATRIX matAncestorWorld = XMMatrixIdentity();
};

XMMATRIX ComputeNodeWorldTransform(int iNodeIndex, const tinygltf::Model& rModel, const std::unordered_map<int, int>& rNodeParentMap)
{
	XMMATRIX matWorld = XMMatrixIdentity();
	int iCurrent = iNodeIndex;

	// Build chain from node to root, then multiply in reverse
	std::vector<XMMATRIX> chain;
	while (iCurrent >= 0)
	{
		const tinygltf::Node& rNode = rModel.nodes[iCurrent];
		XMMATRIX matLocal = XMMatrixIdentity();

		bool bHasTRS = rNode.translation.size() == 3 || rNode.rotation.size() == 4 || rNode.scale.size() == 3;
		bool bHasMatrix = rNode.matrix.size() == 16;

		if (bHasTRS)
		{
			XMVECTOR vecTranslation = rNode.translation.size() == 3 ? XMVectorSet(static_cast<float>(rNode.translation[0]), static_cast<float>(rNode.translation[1]), static_cast<float>(rNode.translation[2]), 0.0f) : XMVectorZero();
			XMVECTOR vecRotation = rNode.rotation.size() == 4 ? XMVectorSet(static_cast<float>(rNode.rotation[0]), static_cast<float>(rNode.rotation[1]), static_cast<float>(rNode.rotation[2]), static_cast<float>(rNode.rotation[3])) : XMQuaternionIdentity();
			XMVECTOR vecScale = rNode.scale.size() == 3 ? XMVectorSet(static_cast<float>(rNode.scale[0]), static_cast<float>(rNode.scale[1]), static_cast<float>(rNode.scale[2]), 1.0f) : XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);

			matLocal = XMMatrixScalingFromVector(vecScale) * XMMatrixRotationQuaternion(vecRotation) * XMMatrixTranslationFromVector(vecTranslation);
		}
		else if (bHasMatrix)
		{
			// glTF stores matrices in column-major order, DirectXMath uses row-major
			// Loading column-major data as row-major puts translation into row 3, which is correct for DirectXMath
			matLocal = XMMATRIX(
				static_cast<float>(rNode.matrix[0]), static_cast<float>(rNode.matrix[1]), static_cast<float>(rNode.matrix[2]), static_cast<float>(rNode.matrix[3]),
				static_cast<float>(rNode.matrix[4]), static_cast<float>(rNode.matrix[5]), static_cast<float>(rNode.matrix[6]), static_cast<float>(rNode.matrix[7]),
				static_cast<float>(rNode.matrix[8]), static_cast<float>(rNode.matrix[9]), static_cast<float>(rNode.matrix[10]), static_cast<float>(rNode.matrix[11]),
				static_cast<float>(rNode.matrix[12]), static_cast<float>(rNode.matrix[13]), static_cast<float>(rNode.matrix[14]), static_cast<float>(rNode.matrix[15]));
		}

		chain.push_back(matLocal);

		auto parentIt = rNodeParentMap.find(iCurrent);
		iCurrent = (parentIt != rNodeParentMap.end()) ? parentIt->second : -1;
	}

	// Multiply from node to root: nodeWorld = nodeLocal * parentWorld = node * parent * ... * root
	for (const XMMATRIX& rMatLocal : chain)
	{
		matWorld = matWorld * rMatLocal;
	}

	return matWorld;
}

AncestorJointResult FindNearestAncestorJoint(Parent* pParent, const std::unordered_map<int, int>& rNodeToJointMap)
{
	AncestorJointResult result;
	Parent* pCurrent = pParent;
	while (pCurrent != nullptr)
	{
		if (pCurrent->iNodeIndex >= 0)
		{
			auto it = rNodeToJointMap.find(pCurrent->iNodeIndex);
			if (it != rNodeToJointMap.end())
			{
				result.iJointIndex = it->second;
				// Compute accumulated world transform from root to this ancestor joint (inclusive)
				result.matAncestorWorld = pCurrent->matNode;
				Parent* pAncestor = pCurrent->pParent;
				while (pAncestor != nullptr)
				{
					result.matAncestorWorld = pAncestor->matNode * result.matAncestorWorld;
					pAncestor = pAncestor->pParent;
				}
				return result;
			}
		}
		pCurrent = pCurrent->pParent;
	}
	return result;
}

// Based on https://github.com/SaschaWillems/Vulkan-glTF-PBR
// rMaterialNodeMap: tracks (originalMaterial, nodeIndex) -> effectiveMaterialIndex for handling primitives from different mesh nodes that share a material
void LoadVertices(Parent* pParent, int iCurrentNodeIndex, const tinygltf::Node& rNode, const tinygltf::Model& rModel, std::vector<common::ModelVertex>& rVertices, std::vector<Material>& rMaterials, const std::unordered_map<int, int>& rNodeToJointMap, std::vector<MaterialNodeInfo>& rMaterialNodeInfos, std::unordered_map<std::pair<int, int>, int, PairHash>& rMaterialNodeMap)
{
	XMMATRIX matNode = XMMatrixIdentity();
	bool bHasTRS = rNode.translation.size() == 3 || rNode.rotation.size() == 4 || rNode.scale.size() == 3;
	bool bHasMatrix = rNode.matrix.size() == 16;

	if (bHasTRS)
	{
		// Build matrix from TRS properties
		XMVECTOR vecTranslation = XMVectorZero();
		XMVECTOR vecRotation = XMQuaternionIdentity();
		XMVECTOR vecScale = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);

		if (rNode.translation.size() == 3)
		{
			vecTranslation = XMVectorSet(static_cast<float>(rNode.translation[0]), static_cast<float>(rNode.translation[1]), static_cast<float>(rNode.translation[2]), 0.0f);
		}
		if (rNode.rotation.size() == 4)
		{
			vecRotation = XMVectorSet(static_cast<float>(rNode.rotation[0]), static_cast<float>(rNode.rotation[1]), static_cast<float>(rNode.rotation[2]), static_cast<float>(rNode.rotation[3]));
		}
		if (rNode.scale.size() == 3)
		{
			vecScale = XMVectorSet(static_cast<float>(rNode.scale[0]), static_cast<float>(rNode.scale[1]), static_cast<float>(rNode.scale[2]), 1.0f);
		}

		XMMATRIX matScale = XMMatrixScalingFromVector(vecScale);
		XMMATRIX matRotation = XMMatrixRotationQuaternion(vecRotation);
		XMMATRIX matTranslation = XMMatrixTranslationFromVector(vecTranslation);
		matNode = matScale * matRotation * matTranslation;
	}
	else if (bHasMatrix)
	{
		// glTF stores matrices in column-major order, DirectXMath uses row-major
		// Loading column-major data as row-major puts translation into row 3, which is correct for DirectXMath
		matNode = XMMATRIX(static_cast<float>(rNode.matrix[0]), static_cast<float>(rNode.matrix[1]), static_cast<float>(rNode.matrix[2]), static_cast<float>(rNode.matrix[3]), static_cast<float>(rNode.matrix[4]), static_cast<float>(rNode.matrix[5]), static_cast<float>(rNode.matrix[6]), static_cast<float>(rNode.matrix[7]), static_cast<float>(rNode.matrix[8]), static_cast<float>(rNode.matrix[9]), static_cast<float>(rNode.matrix[10]), static_cast<float>(rNode.matrix[11]), static_cast<float>(rNode.matrix[12]), static_cast<float>(rNode.matrix[13]), static_cast<float>(rNode.matrix[14]), static_cast<float>(rNode.matrix[15]));
	}

	for (size_t i = 0; i < rNode.children.size(); ++i)
	{
		Parent parent {pParent, matNode, iCurrentNodeIndex};
		LoadVertices(&parent, rNode.children[i], rModel.nodes[rNode.children[i]], rModel, rVertices, rMaterials, rNodeToJointMap, rMaterialNodeInfos, rMaterialNodeMap);
	}

	if (rNode.mesh < 0)
	{
		return;
	}

	XMMATRIX matLocal = XMMatrixIdentity();
	Parent* pCurrentParent = pParent;
	while (pCurrentParent != nullptr)
	{
		matLocal = matLocal * pCurrentParent->matNode;
		pCurrentParent = pCurrentParent->pParent;
	}

	const tinygltf::Mesh& rMesh = rModel.meshes[rNode.mesh];
	for (size_t i = 0; i < rMesh.primitives.size(); ++i)
	{
		const tinygltf::Primitive& rPrimitive = rMesh.primitives[i];
		ASSERT(rPrimitive.attributes.find("POSITION") != rPrimitive.attributes.end());
		ASSERT(rPrimitive.attributes.find("COLOR_0") == rPrimitive.attributes.end());
		if (rPrimitive.attributes.find("TEXCOORD_6") != rPrimitive.attributes.end())
		{
			Log("WARNING: Found TEXCOORD_6");
		}

		ASSERT(rPrimitive.material >= 0);
		int iOriginalMaterial = rPrimitive.material;
		uint32_t vertexStart = static_cast<uint32_t>(rVertices.size());
		bool bHasSkinning = rPrimitive.attributes.find("JOINTS_0") != rPrimitive.attributes.end();

		// Determine effective material index for this (originalMaterial, nodeIndex) combination
		// Primitives from different mesh nodes that share a material need separate material entries
		// to have correct per-primitive mesh world transforms at runtime
		std::pair<int, int> key = std::make_pair(iOriginalMaterial, iCurrentNodeIndex);
		auto it = rMaterialNodeMap.find(key);
		int iEffectiveMaterial = -1;

		if (it != rMaterialNodeMap.end())
		{
			// Already have a material entry for this (originalMaterial, nodeIndex) combination
			iEffectiveMaterial = it->second;
		}
		else if (rMaterialNodeInfos.at(iOriginalMaterial).iNodeIndex < 0)
		{
			// Original material not yet used - use it directly
			iEffectiveMaterial = iOriginalMaterial;
			rMaterialNodeMap[key] = iEffectiveMaterial;
			rMaterialNodeInfos.at(iOriginalMaterial).iNodeIndex = iCurrentNodeIndex;
			rMaterialNodeInfos.at(iOriginalMaterial).matMeshWorld = matNode * matLocal;
			rMaterialNodeInfos.at(iOriginalMaterial).iOriginalMaterialIndex = iOriginalMaterial;
		}
		else if (rMaterialNodeInfos.at(iOriginalMaterial).iNodeIndex == iCurrentNodeIndex)
		{
			// Original material already used by this same node - use it
			iEffectiveMaterial = iOriginalMaterial;
			rMaterialNodeMap[key] = iEffectiveMaterial;
		}
		else
		{
			// Original material used by a different node - create a new split material
			iEffectiveMaterial = static_cast<int>(rMaterials.size());
			rMaterialNodeMap[key] = iEffectiveMaterial;
			rMaterials.emplace_back();
			MaterialNodeInfo newInfo;
			newInfo.bHasSkinning = bHasSkinning;
			newInfo.iNodeIndex = iCurrentNodeIndex;
			newInfo.matMeshWorld = matNode * matLocal;
			newInfo.iOriginalMaterialIndex = iOriginalMaterial;
			rMaterialNodeInfos.push_back(newInfo);
			Log("  Split material {} for node {} -> new material {}", iOriginalMaterial, iCurrentNodeIndex, iEffectiveMaterial);
		}

		Material& rMaterial = rMaterials.at(iEffectiveMaterial);
		MaterialNodeInfo& rMaterialNodeInfo = rMaterialNodeInfos.at(iEffectiveMaterial);

		if (bHasSkinning)
		{
			rMaterialNodeInfo.bHasSkinning = true;
		}

		// Position
		const tinygltf::Accessor& rPositionAccessor = rModel.accessors[rPrimitive.attributes.find("POSITION")->second];
		const tinygltf::BufferView& rPositionBufferView = rModel.bufferViews[rPositionAccessor.bufferView];
		const float* pfPositions = reinterpret_cast<const float*>(&(rModel.buffers[rPositionBufferView.buffer].data[rPositionAccessor.byteOffset + rPositionBufferView.byteOffset]));
		int iPositionStride = rPositionAccessor.ByteStride(rPositionBufferView) ? (rPositionAccessor.ByteStride(rPositionBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);

		// Normal
		const float* pfNormals = nullptr;
		int iNormalStride = 0;
		if (rPrimitive.attributes.find("NORMAL") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("NORMAL")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfNormals = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iNormalStride = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
		}

		// Texcoord 0
		const float* pfTexcoords0 = nullptr;
		int iTexcoordStride0 = 0;
		if (rPrimitive.attributes.find("TEXCOORD_0") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("TEXCOORD_0")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfTexcoords0 = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iTexcoordStride0 = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
		}

		// Texcoord 1
		const float* pfTexcoords1 = nullptr;
		int iTexcoordStride1 = 0;
		if (rPrimitive.attributes.find("TEXCOORD_1") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("TEXCOORD_1")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfTexcoords1 = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iTexcoordStride1 = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
		}

		// Texcoord 2
		const float* pfTexcoords2 = nullptr;
		int iTexcoordStride2 = 0;
		if (rPrimitive.attributes.find("TEXCOORD_2") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("TEXCOORD_2")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfTexcoords2 = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iTexcoordStride2 = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
		}

		// Texcoord 3
		const float* pfTexcoords3 = nullptr;
		int iTexcoordStride3 = 0;
		if (rPrimitive.attributes.find("TEXCOORD_3") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("TEXCOORD_3")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfTexcoords3 = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iTexcoordStride3 = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
		}

		// Texcoord 4
		const float* pfTexcoords4 = nullptr;
		int iTexcoordStride4 = 0;
		if (rPrimitive.attributes.find("TEXCOORD_4") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("TEXCOORD_4")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfTexcoords4 = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iTexcoordStride4 = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
		}

		// Joints
		const uint16_t* puiJoints = nullptr;
		int iJointsStride = 0;
		if (rPrimitive.attributes.find("JOINTS_0") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("JOINTS_0")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			ASSERT(rAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
			puiJoints = reinterpret_cast<const uint16_t*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iJointsStride = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / tinygltf::GetComponentSizeInBytes(rAccessor.componentType)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
		}

		// Weights
		const float* pfWeights = nullptr;
		int iWeightsStride = 0;
		if (rPrimitive.attributes.find("WEIGHTS_0") != rPrimitive.attributes.end())
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rPrimitive.attributes.find("WEIGHTS_0")->second];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfWeights = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
			iWeightsStride = rAccessor.ByteStride(rBufferView) ? (rAccessor.ByteStride(rBufferView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
		}

		// De-duplicate vertices using hash map for O(1) lookups
		std::unordered_map<common::ModelVertex, uint32_t> vertexToIndex;
		std::vector<uint32_t> indexRemap(rPositionAccessor.count);
		rVertices.reserve(rVertices.size() + rPositionAccessor.count);
		for (int64_t j = 0; j < static_cast<int64_t>(rPositionAccessor.count); ++j)
		{
			common::ModelVertex vertex;

			auto vecPosition = XMVectorSet(pfPositions[j * iPositionStride + 0], pfPositions[j * iPositionStride + 1], pfPositions[j * iPositionStride + 2], 1.0f);
			auto vecNormal = pfNormals ? XMVectorSet(pfNormals[j * iNormalStride], pfNormals[j * iNormalStride + 1], pfNormals[j * iNormalStride + 2], 0.0f) : XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

			bool bHasSkeletonData = !rNodeToJointMap.empty();
			if (!bHasSkinning && !bHasSkeletonData)
			{
				// Only transform static models without skeleton data
				// Skinned vertices must remain in local space for runtime skinning pipeline
				XMMATRIX matMeshWorld = matNode * matLocal;
				vecPosition = XMVector4Transform(vecPosition, matMeshWorld);
				vecNormal = XMVector3TransformNormal(vecNormal, matMeshWorld);
			}
			// Non-skinned vertices on animated models: keep in mesh-local space for runtime mesh matrix

			XMStoreFloat3(&vertex.f3Pos, vecPosition);
			XMStoreFloat3(&vertex.f3Normal, XMVector3Normalize(vecNormal));

			vertex.f2Uv = pfTexcoords0 != nullptr ? XMFLOAT2(&pfTexcoords0[j * iTexcoordStride0]) : XMFLOAT2(0.0f, 0.0f);
			vertex.f2Uv1 = pfTexcoords1 != nullptr ? XMFLOAT2(&pfTexcoords1[j * iTexcoordStride1]) : vertex.f2Uv;
			vertex.f2Uv2 = pfTexcoords2 != nullptr ? XMFLOAT2(&pfTexcoords2[j * iTexcoordStride2]) : vertex.f2Uv;
			vertex.f2Uv3 = pfTexcoords3 != nullptr ? XMFLOAT2(&pfTexcoords3[j * iTexcoordStride3]) : vertex.f2Uv;
			vertex.f2Uv4 = pfTexcoords4 != nullptr ? XMFLOAT2(&pfTexcoords4[j * iTexcoordStride4]) : vertex.f2Uv;

			if (puiJoints != nullptr)
			{
				vertex.fJoint = static_cast<float>(puiJoints[j * iJointsStride]);
				vertex.f4Joint0 = XMFLOAT4(
					static_cast<float>(puiJoints[j * iJointsStride + 0]),
					static_cast<float>(puiJoints[j * iJointsStride + 1]),
					static_cast<float>(puiJoints[j * iJointsStride + 2]),
					static_cast<float>(puiJoints[j * iJointsStride + 3]));
				if (pfWeights != nullptr)
				{
					vertex.f4Weight0 = XMFLOAT4(
						pfWeights[j * iWeightsStride + 0],
						pfWeights[j * iWeightsStride + 1],
						pfWeights[j * iWeightsStride + 2],
						pfWeights[j * iWeightsStride + 3]);
				}
			}
			else
			{
				// Non-skinned vertex: store dummy joint data
				// The shader will use mesh matrix from slot 64+materialIndex instead of skinning
				vertex.fJoint = 0.0f;
				vertex.f4Joint0 = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
				vertex.f4Weight0 = XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f);
			}

			auto [jt, bInserted] = vertexToIndex.emplace(vertex, static_cast<uint32_t>(rVertices.size()));
			if (bInserted)
			{
				rVertices.push_back(vertex);
			}
			indexRemap.at(j) = jt->second;
		}

		uint32_t uiNewVertexCount = static_cast<uint32_t>(rVertices.size()) - vertexStart;
		Log("  Vertices: {} -> {} (deduplicated {})", rPositionAccessor.count, uiNewVertexCount, rPositionAccessor.count - uiNewVertexCount);

		if (rPrimitive.indices > -1)
		{
			const tinygltf::Accessor& rIndicesAccessor = rModel.accessors[rPrimitive.indices];
			const tinygltf::BufferView& rIndiciesBufferView = rModel.bufferViews[rIndicesAccessor.bufferView];
			const tinygltf::Buffer& rIndiciesBuffer = rModel.buffers[rIndiciesBufferView.buffer];
			const void* pIndices = &(rIndiciesBuffer.data[rIndicesAccessor.byteOffset + rIndiciesBufferView.byteOffset]);

			rMaterial.indexBuffer.reserve(rMaterial.indexBuffer.size() + rIndicesAccessor.count);
			switch (rIndicesAccessor.componentType)
			{
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
				{
					const uint32_t* puiIndices = static_cast<const uint32_t*>(pIndices);
					for (int64_t j = 0; j < static_cast<int64_t>(rIndicesAccessor.count); ++j)
					{
						rMaterial.indexBuffer.push_back(indexRemap.at(puiIndices[j]));
					}
					break;
				}

				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
				{
					const uint16_t* puiIndices = static_cast<const uint16_t*>(pIndices);
					for (int64_t j = 0; j < static_cast<int64_t>(rIndicesAccessor.count); ++j)
					{
						rMaterial.indexBuffer.push_back(indexRemap.at(puiIndices[j]));
					}
					break;
				}

				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
				{
					const uint8_t* puiIndices = static_cast<const uint8_t*>(pIndices);
					for (int64_t j = 0; j < static_cast<int64_t>(rIndicesAccessor.count); ++j)
					{
						rMaterial.indexBuffer.push_back(indexRemap.at(puiIndices[j]));
					}
					break;
				}

				default:
					ASSERT(false);
					return;
			}
		}
	}
}

std::unordered_map<int, int> BuildNodeParentMap(const tinygltf::Model& rModel)
{
	std::unordered_map<int, int> parentMap;
	for (size_t i = 0; i < rModel.nodes.size(); ++i)
	{
		for (int iChildIndex : rModel.nodes[i].children)
		{
			parentMap[iChildIndex] = static_cast<int>(i);
		}
	}
	return parentMap;
}

bool IsOcclusion(int64_t iIndex, const tinygltf::Material& rMaterial)
{
	bool bOcculsion = rMaterial.additionalValues.find("occlusionTexture") != rMaterial.additionalValues.end() && rMaterial.additionalValues.at("occlusionTexture").TextureIndex() == iIndex;

	// Check if occlusion texture is shared with another texture type (if so, treat as non-occlusion)
	if (bOcculsion && (rMaterial.values.find("baseColorTexture") != rMaterial.values.end() && rMaterial.values.at("baseColorTexture").TextureIndex() == iIndex || rMaterial.additionalValues.find("normalTexture") != rMaterial.additionalValues.end() && rMaterial.additionalValues.at("normalTexture").TextureIndex() == iIndex || rMaterial.values.find("metallicRoughnessTexture") != rMaterial.values.end() && rMaterial.values.at("metallicRoughnessTexture").TextureIndex() == iIndex || rMaterial.additionalValues.find("emissiveTexture") != rMaterial.additionalValues.end() && rMaterial.additionalValues.at("emissiveTexture").TextureIndex() == iIndex))
	{
		bOcculsion = false;
	}

	return bOcculsion;
}

// Determine whether to use skeletal or node-based animation.
// Returns true for skeletal (all channels target skin joints), false for node-based.
static bool DetermineAnimationPath(const tinygltf::Model& rGltfModel)
{
	if (rGltfModel.skins.empty())
	{
		return false;
	}

	const tinygltf::Skin& rSkin = rGltfModel.skins[0];
	std::unordered_set<int> skinJoints(rSkin.joints.begin(), rSkin.joints.end());

	for (const tinygltf::Animation& rAnim : rGltfModel.animations)
	{
		for (const tinygltf::AnimationChannel& rChannel : rAnim.channels)
		{
			if (rChannel.target_node >= 0 && skinJoints.count(rChannel.target_node) == 0)
			{
				return false;
			}
		}
	}
	return true;
}

// Build a skeleton from node hierarchy for models with node-based animation (no skin)
// This now stores ALL nodes, consistent with the skeletal animation path
SkeletonData BuildNodeSkeleton(const tinygltf::Model& rModel, std::unordered_map<int, int>& rNodeToJointMap)
{
	Log("BuildNodeSkeleton: Loading all nodes...");

	std::unordered_map<int, int> parentMap = BuildNodeParentMap(rModel);

	SkeletonData skeletonData;
	skeletonData.skeleton.uiNodeCount = static_cast<uint16_t>(rModel.nodes.size());
	ASSERT(skeletonData.skeleton.uiNodeCount <= common::Skeleton::kiMaxNodes);

	// Load skin joint data if a skin exists (needed for skinned meshes even with node-based animation)
	if (!rModel.skins.empty())
	{
		const tinygltf::Skin& rSkin = rModel.skins[0];
		skeletonData.skeleton.uiSkinJointCount = static_cast<uint16_t>(rSkin.joints.size());
		ASSERT(skeletonData.skeleton.uiSkinJointCount <= common::Skeleton::kiMaxSkinJoints);

		// Build skin joint to node index mapping
		skeletonData.skinJointToNode.resize(rSkin.joints.size());
		for (int64_t i = 0; i < static_cast<int64_t>(rSkin.joints.size()); ++i)
		{
			skeletonData.skinJointToNode[i] = static_cast<uint16_t>(rSkin.joints[i]);
		}

		// Load inverse bind matrices from accessor
		const float* pfInverseBindMatrices = nullptr;
		if (rSkin.inverseBindMatrices >= 0)
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rSkin.inverseBindMatrices];
			const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
			pfInverseBindMatrices = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
		}

		skeletonData.inverseBindMatrices.resize(rSkin.joints.size());
		for (int64_t i = 0; i < static_cast<int64_t>(rSkin.joints.size()); ++i)
		{
			if (pfInverseBindMatrices != nullptr)
			{
				const float* pMatrix = &pfInverseBindMatrices[i * 16];
				XMMATRIX matInverseBind = XMMATRIX(pMatrix);
				XMStoreFloat4x4(&skeletonData.inverseBindMatrices[i], matInverseBind);
			}
			else
			{
				XMStoreFloat4x4(&skeletonData.inverseBindMatrices[i], XMMatrixIdentity());
			}
		}

		Log("BuildNodeSkeleton: Loaded {} skin joints from skin", skeletonData.skeleton.uiSkinJointCount);
	}
	else
	{
		skeletonData.skeleton.uiSkinJointCount = 0;
	}

	// Build nodeToJointMap (identity mapping for node-based animation)
	rNodeToJointMap.clear();
	for (int64_t i = 0; i < static_cast<int64_t>(rModel.nodes.size()); ++i)
	{
		rNodeToJointMap[static_cast<int>(i)] = static_cast<int>(i);
	}

	Log("  Total nodes: {}", skeletonData.skeleton.uiNodeCount);

	// Process ALL nodes
	skeletonData.nodes.resize(rModel.nodes.size());
	for (int64_t i = 0; i < static_cast<int64_t>(rModel.nodes.size()); ++i)
	{
		common::ModelNode& rNode = skeletonData.nodes[i];
		const tinygltf::Node& rGltfNode = rModel.nodes[i];

		// Set parent index directly (node index)
		rNode.iParentIndex = -1;
		auto parentIt = parentMap.find(static_cast<int>(i));
		if (parentIt != parentMap.end())
		{
			rNode.iParentIndex = static_cast<int16_t>(parentIt->second);
		}

		// Load bind pose TRS
		if (rGltfNode.translation.size() == 3)
		{
			rNode.f4BindTranslation = XMFLOAT4(static_cast<float>(rGltfNode.translation[0]), static_cast<float>(rGltfNode.translation[1]), static_cast<float>(rGltfNode.translation[2]), 0.0f);
		}
		else
		{
			rNode.f4BindTranslation = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		}

		if (rGltfNode.rotation.size() == 4)
		{
			rNode.f4BindRotation = XMFLOAT4(static_cast<float>(rGltfNode.rotation[0]), static_cast<float>(rGltfNode.rotation[1]), static_cast<float>(rGltfNode.rotation[2]), static_cast<float>(rGltfNode.rotation[3]));
		}
		else
		{
			rNode.f4BindRotation = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		}

		if (rGltfNode.scale.size() == 3)
		{
			rNode.f4BindScale = XMFLOAT4(static_cast<float>(rGltfNode.scale[0]), static_cast<float>(rGltfNode.scale[1]), static_cast<float>(rGltfNode.scale[2]), 1.0f);
		}
		else
		{
			rNode.f4BindScale = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		// Load node matrix (identity if not present)
		if (rGltfNode.matrix.size() == 16)
		{
			// glTF stores matrices in column-major order, DirectXMath uses row-major
			// Loading column-major data as row-major puts translation into row 3, which is correct for DirectXMath
			XMMATRIX matNode = XMMATRIX(
				static_cast<float>(rGltfNode.matrix[0]), static_cast<float>(rGltfNode.matrix[1]), static_cast<float>(rGltfNode.matrix[2]), static_cast<float>(rGltfNode.matrix[3]),
				static_cast<float>(rGltfNode.matrix[4]), static_cast<float>(rGltfNode.matrix[5]), static_cast<float>(rGltfNode.matrix[6]), static_cast<float>(rGltfNode.matrix[7]),
				static_cast<float>(rGltfNode.matrix[8]), static_cast<float>(rGltfNode.matrix[9]), static_cast<float>(rGltfNode.matrix[10]), static_cast<float>(rGltfNode.matrix[11]),
				static_cast<float>(rGltfNode.matrix[12]), static_cast<float>(rGltfNode.matrix[13]), static_cast<float>(rGltfNode.matrix[14]), static_cast<float>(rGltfNode.matrix[15]));
			XMStoreFloat4x4(&rNode.f4x4BindMatrix, matNode);
		}
		else
		{
			XMStoreFloat4x4(&rNode.f4x4BindMatrix, XMMatrixIdentity());
		}
	}

	return skeletonData;
}

SkeletonData LoadSkeleton(const tinygltf::Model& rModel, int32_t iSkinIndex)
{
	SkeletonData skeletonData;
	const tinygltf::Skin& rSkin = rModel.skins[iSkinIndex];

	// Store ALL nodes (not just skin joints)
	skeletonData.skeleton.uiNodeCount = static_cast<uint16_t>(rModel.nodes.size());
	ASSERT(skeletonData.skeleton.uiNodeCount <= common::Skeleton::kiMaxNodes);

	// Build skin joint to node index mapping
	skeletonData.skeleton.uiSkinJointCount = static_cast<uint16_t>(rSkin.joints.size());
	ASSERT(skeletonData.skeleton.uiSkinJointCount <= common::Skeleton::kiMaxSkinJoints);
	skeletonData.skinJointToNode.resize(rSkin.joints.size());
	for (int64_t i = 0; i < static_cast<int64_t>(rSkin.joints.size()); ++i)
	{
		skeletonData.skinJointToNode[i] = static_cast<uint16_t>(rSkin.joints[i]);
	}

	// Build parent map for ALL nodes
	std::unordered_map<int64_t, int64_t> parentMap;
	for (int64_t j = 0; j < static_cast<int64_t>(rModel.nodes.size()); ++j)
	{
		for (int64_t iChildIdx : rModel.nodes[j].children)
		{
			parentMap[iChildIdx] = j;
		}
	}

	// Load inverse bind matrices from accessor
	const float* pfInverseBindMatrices = nullptr;
	if (rSkin.inverseBindMatrices >= 0)
	{
		const tinygltf::Accessor& rAccessor = rModel.accessors[rSkin.inverseBindMatrices];
		const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
		pfInverseBindMatrices = reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
	}

	// Load inverse bind matrices for skin joints only
	skeletonData.inverseBindMatrices.resize(rSkin.joints.size());
	for (int64_t i = 0; i < static_cast<int64_t>(rSkin.joints.size()); ++i)
	{
		if (pfInverseBindMatrices != nullptr)
		{
			// glTF stores matrices in column-major order, DirectXMath uses row-major
			// Loading column-major data as row-major puts translation (indices 12-15) into row 3,
			// which is correct for DirectXMath row-vectors (translation at ._41, ._42, ._43)
			const float* pMatrix = &pfInverseBindMatrices[i * 16];
			XMMATRIX matInverseBind = XMMATRIX(pMatrix);
			XMStoreFloat4x4(&skeletonData.inverseBindMatrices[i], matInverseBind);
		}
		else
		{
			XMStoreFloat4x4(&skeletonData.inverseBindMatrices[i], XMMatrixIdentity());
		}
	}

	// Process ALL nodes
	skeletonData.nodes.resize(rModel.nodes.size());
	for (int64_t i = 0; i < static_cast<int64_t>(rModel.nodes.size()); ++i)
	{
		common::ModelNode& rNode = skeletonData.nodes[i];
		const tinygltf::Node& rGltfNode = rModel.nodes[i];

		// Set parent index directly (node index, not joint index)
		rNode.iParentIndex = -1;
		auto parentIt = parentMap.find(i);
		if (parentIt != parentMap.end())
		{
			rNode.iParentIndex = static_cast<int16_t>(parentIt->second);
		}

		// Load bind pose TRS
		if (rGltfNode.translation.size() == 3)
		{
			rNode.f4BindTranslation = XMFLOAT4(static_cast<float>(rGltfNode.translation[0]), static_cast<float>(rGltfNode.translation[1]), static_cast<float>(rGltfNode.translation[2]), 0.0f);
		}
		else
		{
			rNode.f4BindTranslation = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		}

		if (rGltfNode.rotation.size() == 4)
		{
			rNode.f4BindRotation = XMFLOAT4(static_cast<float>(rGltfNode.rotation[0]), static_cast<float>(rGltfNode.rotation[1]), static_cast<float>(rGltfNode.rotation[2]), static_cast<float>(rGltfNode.rotation[3]));
		}
		else
		{
			rNode.f4BindRotation = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		}

		if (rGltfNode.scale.size() == 3)
		{
			rNode.f4BindScale = XMFLOAT4(static_cast<float>(rGltfNode.scale[0]), static_cast<float>(rGltfNode.scale[1]), static_cast<float>(rGltfNode.scale[2]), 1.0f);
		}
		else
		{
			rNode.f4BindScale = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		// Load node matrix (identity if not present)
		if (rGltfNode.matrix.size() == 16)
		{
			// glTF stores matrices in column-major order, DirectXMath uses row-major
			// Loading column-major data as row-major puts translation into row 3, which is correct for DirectXMath
			XMMATRIX matNode = XMMATRIX(
				static_cast<float>(rGltfNode.matrix[0]), static_cast<float>(rGltfNode.matrix[1]), static_cast<float>(rGltfNode.matrix[2]), static_cast<float>(rGltfNode.matrix[3]),
				static_cast<float>(rGltfNode.matrix[4]), static_cast<float>(rGltfNode.matrix[5]), static_cast<float>(rGltfNode.matrix[6]), static_cast<float>(rGltfNode.matrix[7]),
				static_cast<float>(rGltfNode.matrix[8]), static_cast<float>(rGltfNode.matrix[9]), static_cast<float>(rGltfNode.matrix[10]), static_cast<float>(rGltfNode.matrix[11]),
				static_cast<float>(rGltfNode.matrix[12]), static_cast<float>(rGltfNode.matrix[13]), static_cast<float>(rGltfNode.matrix[14]), static_cast<float>(rGltfNode.matrix[15]));
			XMStoreFloat4x4(&rNode.f4x4BindMatrix, matNode);
		}
		else
		{
			XMStoreFloat4x4(&rNode.f4x4BindMatrix, XMMatrixIdentity());
		}
	}

	return skeletonData;
}

void LoadAnimations(const tinygltf::Model& rModel, const std::unordered_map<int, int>& rNodeToNodeIndexMap, std::vector<common::AnimationClip>& rAnimations, std::vector<common::AnimationChannel>& rChannels, std::vector<common::AnimationKeyframe>& rKeyframes, std::vector<common::AnimationKeyframeCubic>& rCubicKeyframes)
{
	Log("LoadAnimations: nodeToNodeIndexMap has {} entries", rNodeToNodeIndexMap.size());

	int iAnimIndex = 0;
	for (const tinygltf::Animation& rAnim : rModel.animations)
	{
		common::AnimationClip animation {};

		// Set name
		size_t iNameLength = std::min(rAnim.name.size(), static_cast<size_t>(common::AnimationClip::kiMaxNameLength - 1));
		std::memcpy(animation.pcName, rAnim.name.c_str(), iNameLength);
		animation.pcName[iNameLength] = '\0';

		animation.uiChannelStart = static_cast<uint32_t>(rChannels.size());
		animation.uiChannelCount = 0;
		animation.fDuration = 0.0f;

		int iFilteredCount = 0;
		int iKeptCount = 0;
		for (const tinygltf::AnimationChannel& rGltfChannel : rAnim.channels)
		{
			// Skip channels for nodes not in the node map
			auto it = rNodeToNodeIndexMap.find(rGltfChannel.target_node);
			if (it == rNodeToNodeIndexMap.end())
			{
				++iFilteredCount;
				Log("  FILTERED: channel targeting node {} (\"{}\") not in map", rGltfChannel.target_node, rGltfChannel.target_node >= 0 ? rModel.nodes[rGltfChannel.target_node].name : "invalid");
				continue;
			}
			++iKeptCount;
			Log("  KEPT: channel targeting node {} (\"{}\") -> node index {}", rGltfChannel.target_node, rModel.nodes[rGltfChannel.target_node].name, it->second);

			const tinygltf::AnimationSampler& rSampler = rAnim.samplers[rGltfChannel.sampler];

			common::AnimationChannel channel {};
			channel.uiNodeIndex = static_cast<uint16_t>(it->second);

			// Target path
			if (rGltfChannel.target_path == "translation")
			{
				channel.uiTargetPath = 0;
			}
			else if (rGltfChannel.target_path == "rotation")
			{
				channel.uiTargetPath = 1;
			}
			else if (rGltfChannel.target_path == "scale")
			{
				channel.uiTargetPath = 2;
			}
			else
			{
				continue; // Skip unknown paths
			}

			// Interpolation
			if (rSampler.interpolation == "STEP")
			{
				channel.uiInterpolation = 0;
			}
			else if (rSampler.interpolation == "CUBICSPLINE")
			{
				channel.uiInterpolation = 2;
			}
			else
			{
				channel.uiInterpolation = 1; // LINEAR
			}

			// Load keyframe times from input accessor
			const tinygltf::Accessor& rInputAccessor = rModel.accessors[rSampler.input];
			const tinygltf::BufferView& rInputBufferView = rModel.bufferViews[rInputAccessor.bufferView];
			const float* pfTimes = reinterpret_cast<const float*>(&(rModel.buffers[rInputBufferView.buffer].data[rInputAccessor.byteOffset + rInputBufferView.byteOffset]));

			// Load keyframe values from output accessor
			const tinygltf::Accessor& rOutputAccessor = rModel.accessors[rSampler.output];
			const tinygltf::BufferView& rOutputBufferView = rModel.bufferViews[rOutputAccessor.bufferView];
			const float* pfValues = reinterpret_cast<const float*>(&(rModel.buffers[rOutputBufferView.buffer].data[rOutputAccessor.byteOffset + rOutputBufferView.byteOffset]));

			channel.uiKeyframeCount = static_cast<uint32_t>(rInputAccessor.count);

			int iValueStride = (channel.uiTargetPath == 1) ? 4 : 3; // Rotation is vec4, others vec3

			if (channel.uiInterpolation == 2) // CUBICSPLINE
			{
				channel.uiKeyframeStart = static_cast<uint32_t>(rCubicKeyframes.size());

				for (int64_t j = 0; j < static_cast<int64_t>(rInputAccessor.count); ++j)
				{
					common::AnimationKeyframeCubic keyframe {};
					keyframe.fTime = pfTimes[j];

					// CUBICSPLINE has 3 values per keyframe: in-tangent, value, out-tangent
					int64_t iBaseIdx = j * 3 * iValueStride;

					if (channel.uiTargetPath == 1) // Rotation (vec4)
					{
						keyframe.f4InTangent = XMFLOAT4(pfValues[iBaseIdx + 0], pfValues[iBaseIdx + 1], pfValues[iBaseIdx + 2], pfValues[iBaseIdx + 3]);
						keyframe.f4Value = XMFLOAT4(pfValues[iBaseIdx + iValueStride + 0], pfValues[iBaseIdx + iValueStride + 1], pfValues[iBaseIdx + iValueStride + 2], pfValues[iBaseIdx + iValueStride + 3]);
						keyframe.f4OutTangent = XMFLOAT4(pfValues[iBaseIdx + 2 * iValueStride + 0], pfValues[iBaseIdx + 2 * iValueStride + 1], pfValues[iBaseIdx + 2 * iValueStride + 2], pfValues[iBaseIdx + 2 * iValueStride + 3]);
					}
					else // Translation/Scale (vec3)
					{
						keyframe.f4InTangent = XMFLOAT4(pfValues[iBaseIdx + 0], pfValues[iBaseIdx + 1], pfValues[iBaseIdx + 2], 0.0f);
						keyframe.f4Value = XMFLOAT4(pfValues[iBaseIdx + iValueStride + 0], pfValues[iBaseIdx + iValueStride + 1], pfValues[iBaseIdx + iValueStride + 2], 0.0f);
						keyframe.f4OutTangent = XMFLOAT4(pfValues[iBaseIdx + 2 * iValueStride + 0], pfValues[iBaseIdx + 2 * iValueStride + 1], pfValues[iBaseIdx + 2 * iValueStride + 2], 0.0f);
					}

					animation.fDuration = std::max(animation.fDuration, keyframe.fTime);
					rCubicKeyframes.push_back(keyframe);
				}
			}
			else // STEP or LINEAR
			{
				channel.uiKeyframeStart = static_cast<uint32_t>(rKeyframes.size());

				for (int64_t j = 0; j < static_cast<int64_t>(rInputAccessor.count); ++j)
				{
					common::AnimationKeyframe keyframe {};
					keyframe.fTime = pfTimes[j];

					if (channel.uiTargetPath == 1)
					{
						// Rotation (quaternion)
						keyframe.f4Value = XMFLOAT4(pfValues[j * iValueStride + 0], pfValues[j * iValueStride + 1], pfValues[j * iValueStride + 2], pfValues[j * iValueStride + 3]);
					}
					else if (channel.uiTargetPath == 0)
					{
						// Translation
						keyframe.f4Value = XMFLOAT4(pfValues[j * iValueStride + 0], pfValues[j * iValueStride + 1], pfValues[j * iValueStride + 2], 0.0f);
					}
					else
					{
						// Scale
						keyframe.f4Value = XMFLOAT4(pfValues[j * iValueStride + 0], pfValues[j * iValueStride + 1], pfValues[j * iValueStride + 2], 0.0f);
					}

					animation.fDuration = std::max(animation.fDuration, keyframe.fTime);
					rKeyframes.push_back(keyframe);
				}
			}

			rChannels.push_back(channel);
			++animation.uiChannelCount;
		}

		if (animation.uiChannelCount > 0)
		{
			rAnimations.push_back(animation);
		}
		++iAnimIndex;
	}
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
		Log("PreExport Gltf: {}", mInputPath.string());

		ASSERT(gltfModel.textures.size() <= common::SceneHeader::kiMaxTextures);
		Log("Pre-processing {} textures", gltfModel.textures.size());

		// Pre-compute occlusion flags
		std::vector<bool> occlusionFlags;
		occlusionFlags.reserve(gltfModel.textures.size());
		for (const tinygltf::Texture& rTexture : gltfModel.textures)
		{
			bool bOcclusion = false;
			for (const tinygltf::Material& rMaterial : gltfModel.materials)
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
		futures.reserve(gltfModel.textures.size());
		for (size_t i = 0; i < gltfModel.textures.size(); ++i)
		{
			bool bOcclusion = occlusionFlags.at(i);
			const tinygltf::Image& rImage = gltfModel.images.at(gltfModel.textures.at(i).source);
			std::filesystem::path path = GetTextureIntermediatePath(gltfModel.textures.at(i).source, bOcclusion);

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
		for (size_t i = 0; i < gltfModel.textures.size(); ++i)
		{
			futures.at(i).get();
			const tinygltf::Image& rImage = gltfModel.images.at(gltfModel.textures.at(i).source);
			std::filesystem::path path = GetTextureIntermediatePath(gltfModel.textures.at(i).source, occlusionFlags.at(i));
			mIntermediateFiles.push_back(path);
			Log("  {}: Texture {} -> {}", iTextureIndex++, rImage.uri, path.filename().native());
		}

		std::filesystem::path path(mInputPath);
		path += ".MODEL";
		Log("Loading {} materials", gltfModel.materials.size());
		std::vector<Material> materials(gltfModel.materials.size());
		std::vector<MaterialNodeInfo> materialNodeInfos(gltfModel.materials.size());

		// Build nodeToNodeIndexMap - identity mapping since we store all nodes
		std::unordered_map<int, int> nodeToJointMap;
		bool bNodeBasedAnimation = false;

		bool bUseSkeletalAnimation = DetermineAnimationPath(gltfModel);

		if (bUseSkeletalAnimation)
		{
			// Identity mapping - all nodes stored
			for (int64_t i = 0; i < static_cast<int64_t>(gltfModel.nodes.size()); ++i)
			{
				nodeToJointMap[static_cast<int>(i)] = static_cast<int>(i);
			}
			Log("  Skeletal animation detected: {} skin joints, {} total nodes", gltfModel.skins[0].joints.size(), gltfModel.nodes.size());
		}
		else if (gltfModel.animations.size() > 0)
		{
			// Node-based animation: build skeleton from node hierarchy (also uses identity mapping)
			bNodeBasedAnimation = true;
			SkeletonData tempSkeletonData = BuildNodeSkeleton(gltfModel, nodeToJointMap);
			Log("  Node-based animation detected: {} nodes in skeleton", tempSkeletonData.skeleton.uiNodeCount);
		}

		const tinygltf::Scene& rScene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
		std::vector<common::ModelVertex> vertices;
		std::unordered_map<std::pair<int, int>, int, PairHash> materialNodeMap;
		for (size_t i = 0; i < rScene.nodes.size(); ++i)
		{
			int iNodeIndex = rScene.nodes[i];
			const tinygltf::Node& rNode = gltfModel.nodes[iNodeIndex];
			Parent parent {nullptr, XMMatrixIdentity(), -1};
			LoadVertices(&parent, iNodeIndex, rNode, gltfModel, vertices, materials, nodeToJointMap, materialNodeInfos, materialNodeMap);
		}
		if (materials.size() > gltfModel.materials.size())
		{
			Log("  Split {} materials into {} to handle primitives from different mesh nodes", gltfModel.materials.size(), materials.size());
		}

		// Compute relative transforms for non-skinned materials and set jointCount
		std::vector<common::MaterialInfo> materialInfos(materials.size());

		// Build parent map for node hierarchy traversal (used for both skeletal and node-based)
		std::unordered_map<int, int> nodeParentMap = BuildNodeParentMap(gltfModel);

		// Get skin joint count for skinned materials (always use skin's joint count, not animation node count)
		uint8_t uiSkinJointCount = 0;
		if (!gltfModel.skins.empty())
		{
			size_t jointCount = gltfModel.skins[0].joints.size();
			if (jointCount > common::kiMaxJointsPerMesh)
			{
				Log("WARNING: Model has {} joints, exceeding shader limit of {}. Skinning will use first {} joints only.",
					jointCount, common::kiMaxJointsPerMesh, common::kiMaxJointsPerMesh);
			}
			uiSkinJointCount = static_cast<uint8_t>(jointCount);
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
						matAncestorWorld = ComputeNodeWorldTransform(iCurrentNode, gltfModel, nodeParentMap);
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
			tinygltf::Material& tinygltfMaterial = gltfModel.materials[iOrigMat];
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
			std::fstream fileStreamOut(preExportPath, std::ios::out | std::ios::binary);
			int64_t iVersion = GetVersion();
			fileStreamOut.write(reinterpret_cast<const char*>(&iVersion), sizeof(iVersion));
			fileStreamOut.flush();
			fileStreamOut.close();
			mIntermediateFiles.push_back(preExportPath);
		}

		Log("Samplers: {}", gltfModel.samplers.size());
		for (const tinygltf::Sampler& rSampler : gltfModel.samplers)
		{
			Log("  {} {} {} {}", ToVkFilter(rSampler.minFilter), ToVkFilter(rSampler.magFilter), ToVkSamplerAddressMode(rSampler.wrapS), ToVkSamplerAddressMode(rSampler.wrapT));
			ASSERT(ToVkSamplerAddressMode(rSampler.wrapS) == VK_SAMPLER_ADDRESS_MODE_REPEAT);
		}
	}

	// Read material count from .MODEL file first (may be larger than gltfModel.materials.size() due to splitting)
	std::filesystem::path modelPath(mInputPath);
	modelPath += ".MODEL";
	size_t uiMaterialCount = 0;
	std::fstream materialCountFileStream(modelPath, std::ios::in | std::ios::binary);
	materialCountFileStream.read(reinterpret_cast<char*>(&uiMaterialCount), sizeof(uiMaterialCount));
	materialCountFileStream.close();

	// Scene chunk data layout: [textureCrcs ALIGN16] [indexStarts ALIGN16] [MaterialShaderData]
	int64_t iTextureArraySize = common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(gltfModel.textures.size()) * static_cast<int64_t>(sizeof(common::crc_t)));
	int64_t iIndexStartsSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(uiMaterialCount) * static_cast<int64_t>(sizeof(uint32_t)));
	int64_t iSceneArraysSize = iTextureArraySize + iIndexStartsSize;
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iSceneArraysSize + uiMaterialCount * sizeof(common::MaterialShaderData));
	common::crc_t* pTextureCrcs = reinterpret_cast<common::crc_t*>(dataSpan.data());
	uint32_t* puiIndexStarts = reinterpret_cast<uint32_t*>(dataSpan.data() + iTextureArraySize);
	common::MaterialShaderData* pMaterialShaderDatas = reinterpret_cast<common::MaterialShaderData*>(dataSpan.data() + iSceneArraysSize);

	Log("Textures: {}", gltfModel.textures.size());
	pHeader->sceneHeader.uiTextureCount = 0;
	for (const tinygltf::Texture& rTexture : gltfModel.textures)
	{
		bool bOcclusion = false;
		for (const tinygltf::Material& rMaterial : gltfModel.materials)
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

	Log("Materials: {} (original), {} (after splitting)", gltfModel.materials.size(), uiMaterialCount);

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
		const tinygltf::Material& rMaterial = gltfModel.materials[iOrigMat];
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
	if (gltfModel.animations.size() > 0)
	{
		// Log animation overview
		Log("Animation export: {} skins, {} animations", gltfModel.skins.size(), gltfModel.animations.size());
		if (gltfModel.skins.size() > 0)
		{
			Log("  Skin 0 has {} joints", gltfModel.skins[0].joints.size());
		}
		int iTotalChannels = 0;
		for (const tinygltf::Animation& rAnim : gltfModel.animations)
		{
			iTotalChannels += static_cast<int>(rAnim.channels.size());
		}
		Log("  Total animation channels in glTF: {}", iTotalChannels);

		pHeader->sceneHeader.bHasAnimation = true;
		SkeletonData skeletonData;
		std::unordered_map<int, int> nodeToJointMap;

		bool bUseSkeletalAnimation = DetermineAnimationPath(gltfModel);

		Log("  Using {} animation path", bUseSkeletalAnimation ? "SKELETAL" : "NODE-BASED");

		if (bUseSkeletalAnimation)
		{
			// Skeletal animation path
			Log("Loading skeletal animation...");
			skeletonData = LoadSkeleton(gltfModel, 0);

			// Build nodeToNodeIndexMap - identity mapping since we store all nodes
			for (int64_t i = 0; i < static_cast<int64_t>(gltfModel.nodes.size()); ++i)
			{
				nodeToJointMap[static_cast<int>(i)] = static_cast<int>(i);
			}
		}
		else
		{
			// Node-based animation path (no skin, or skin joints don't match animated nodes)
			Log("Loading node-based animation...");
			skeletonData = BuildNodeSkeleton(gltfModel, nodeToJointMap);
		}

		Log("  {} nodes in skeleton, {} skin joints", skeletonData.skeleton.uiNodeCount, skeletonData.skeleton.uiSkinJointCount);

		// Load animations
		std::vector<common::AnimationClip> animations;
		std::vector<common::AnimationChannel> channels;
		std::vector<common::AnimationKeyframe> keyframes;
		std::vector<common::AnimationKeyframeCubic> cubicKeyframes;
		LoadAnimations(gltfModel, nodeToJointMap, animations, channels, keyframes, cubicKeyframes);
		Log("  {} animations, {} channels, {} keyframes, {} cubic keyframes", animations.size(), channels.size(), keyframes.size(), cubicKeyframes.size());

		// Warn if all animation channels were filtered out
		if (animations.empty() && gltfModel.animations.size() > 0)
		{
			Log("WARNING: All animation channels were filtered out!");
			Log("  nodeToJointMap size: {}", nodeToJointMap.size());
			// Log first few entries of nodeToJointMap
			int iCount = 0;
			for (const auto& [iNode, iJoint] : nodeToJointMap)
			{
				Log("    nodeToJointMap[{}] (\"{}\") = {}", iNode, gltfModel.nodes[iNode].name, iJoint);
				if (++iCount >= 10)
				{
					break;
				}
			}
			// Log first few animation channel targets
			iCount = 0;
			for (const tinygltf::Animation& rAnim : gltfModel.animations)
			{
				for (const tinygltf::AnimationChannel& rChannel : rAnim.channels)
				{
					Log("    Animation channel targets node {} (\"{}\")", rChannel.target_node, rChannel.target_node >= 0 ? gltfModel.nodes[rChannel.target_node].name : "invalid");
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
