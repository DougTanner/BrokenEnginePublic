#include "ExportGltf.h"

#include "Texture.h"

using enum common::ChunkFlags;

tinygltf::TinyGLTF gGltfContext;

std::optional<common::ChunkFlags_t> ExportGltf::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	return rDirectoryEntry.path().extension() == ".gltf" ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kGltf) : std::nullopt;
}

bool ExportGltf::CheckDirty(const std::filesystem::path& rPackFile)
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

std::filesystem::path ExportGltf::GetPreExportMarkerPath() const
{
	std::filesystem::path path(mInputPath);
	path += ".PreExport";
	return path;
}

std::filesystem::path ExportGltf::GetTextureIntermediatePath(int64_t iTextureIndex, bool bOcclusion) const
{
	std::filesystem::path path(mInputPath);
	path += ".Texture";
	path += std::to_string(iTextureIndex);
	path += bOcclusion ? ".BC4_UNORM_BLOCK" : ".BC7_UNORM_BLOCK";
	return path;
}

tinygltf::Model ExportGltf::LoadGltfModel()
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
	std::vector<common::GltfVertex> vertexBuffer;
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
			// Loading column-major as row-major effectively reads the transpose, so transpose to get the correct matrix
			XMMATRIX matTemp = XMMATRIX(
				static_cast<float>(rNode.matrix[0]), static_cast<float>(rNode.matrix[1]), static_cast<float>(rNode.matrix[2]), static_cast<float>(rNode.matrix[3]),
				static_cast<float>(rNode.matrix[4]), static_cast<float>(rNode.matrix[5]), static_cast<float>(rNode.matrix[6]), static_cast<float>(rNode.matrix[7]),
				static_cast<float>(rNode.matrix[8]), static_cast<float>(rNode.matrix[9]), static_cast<float>(rNode.matrix[10]), static_cast<float>(rNode.matrix[11]),
				static_cast<float>(rNode.matrix[12]), static_cast<float>(rNode.matrix[13]), static_cast<float>(rNode.matrix[14]), static_cast<float>(rNode.matrix[15]));
			matLocal = XMMatrixTranspose(matTemp);
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
void LoadVertices(Parent* pParent, int iCurrentNodeIndex, const tinygltf::Node& rNode, const tinygltf::Model& rModel, std::vector<Material>& rMaterials, const std::unordered_map<int, int>& rNodeToJointMap, std::vector<MaterialNodeInfo>& rMaterialNodeInfos)
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
		// Loading column-major as row-major effectively reads the transpose, so transpose to get the correct matrix
		XMMATRIX matTemp = XMMATRIX(static_cast<float>(rNode.matrix[0]), static_cast<float>(rNode.matrix[1]), static_cast<float>(rNode.matrix[2]), static_cast<float>(rNode.matrix[3]), static_cast<float>(rNode.matrix[4]), static_cast<float>(rNode.matrix[5]), static_cast<float>(rNode.matrix[6]), static_cast<float>(rNode.matrix[7]), static_cast<float>(rNode.matrix[8]), static_cast<float>(rNode.matrix[9]), static_cast<float>(rNode.matrix[10]), static_cast<float>(rNode.matrix[11]), static_cast<float>(rNode.matrix[12]), static_cast<float>(rNode.matrix[13]), static_cast<float>(rNode.matrix[14]), static_cast<float>(rNode.matrix[15]));
		matNode = XMMatrixTranspose(matTemp);
	}

	for (size_t i = 0; i < rNode.children.size(); ++i)
	{
		Parent parent {pParent, matNode, iCurrentNodeIndex};
		LoadVertices(&parent, rNode.children[i], rModel.nodes[rNode.children[i]], rModel, rMaterials, rNodeToJointMap, rMaterialNodeInfos);
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
		Assert(rPrimitive.attributes.find("POSITION") != rPrimitive.attributes.end());
		Assert(rPrimitive.attributes.find("COLOR_0") == rPrimitive.attributes.end());
		if (rPrimitive.attributes.find("TEXCOORD_6") != rPrimitive.attributes.end())
		{
			Log("WARNING: Found TEXCOORD_6");
		}

		Assert(rPrimitive.material >= 0);
		Material& rMaterial = rMaterials[rPrimitive.material];
		MaterialNodeInfo& rMaterialNodeInfo = rMaterialNodeInfos[rPrimitive.material];
		int64_t iVertexStart = rMaterial.vertexBuffer.size();

		// Check if this primitive has skinning data
		bool bHasSkinning = rPrimitive.attributes.find("JOINTS_0") != rPrimitive.attributes.end();
		if (bHasSkinning)
		{
			rMaterialNodeInfo.bHasSkinning = true;
		}
		else if (rMaterialNodeInfo.iNodeIndex < 0)
		{
			// Record non-skinned material's mesh node info for relative transform computation
			rMaterialNodeInfo.iNodeIndex = iCurrentNodeIndex;
			rMaterialNodeInfo.matMeshWorld = matNode * matLocal;
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
			Assert(rAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
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

		rMaterial.vertexBuffer.reserve(rMaterial.vertexBuffer.size() + rPositionAccessor.count);
		for (int64_t j = 0; j < static_cast<int64_t>(rPositionAccessor.count); ++j)
		{
			common::GltfVertex& rVertex = rMaterial.vertexBuffer.emplace_back();

			auto vecPosition = XMVectorSet(pfPositions[j * iPositionStride + 0], pfPositions[j * iPositionStride + 1], pfPositions[j * iPositionStride + 2], 1.0f);
			auto vecNormal = pfNormals ? XMVectorSet(pfNormals[j * iNormalStride], pfNormals[j * iNormalStride + 1], pfNormals[j * iNormalStride + 2], 0.0f) : XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

			bool bHasSkeletonData = !rNodeToJointMap.empty();
			if (bHasSkinning || !bHasSkeletonData)
			{
				// Transform to world-bind-pose space using full mesh world matrix (node + parents)
				// - Skinned vertices: need full world transform for proper joint matrix application
				// - Static models without skeleton: no runtime mesh matrices available
				XMMATRIX matMeshWorld = matNode * matLocal;
				vecPosition = XMVector4Transform(vecPosition, matMeshWorld);
				vecNormal = XMVector3TransformNormal(vecNormal, matMeshWorld);
			}
			// Non-skinned vertices on animated models: keep in mesh-local space for runtime mesh matrix

			XMStoreFloat3(&rVertex.f3Pos, vecPosition);
			XMStoreFloat3(&rVertex.f3Normal, XMVector3Normalize(vecNormal));

			rVertex.f2Uv = pfTexcoords0 != nullptr ? XMFLOAT2(&pfTexcoords0[j * iTexcoordStride0]) : XMFLOAT2(0.0f, 0.0f);
			rVertex.f2Uv1 = pfTexcoords1 != nullptr ? XMFLOAT2(&pfTexcoords1[j * iTexcoordStride1]) : rVertex.f2Uv;
			rVertex.f2Uv2 = pfTexcoords2 != nullptr ? XMFLOAT2(&pfTexcoords2[j * iTexcoordStride2]) : rVertex.f2Uv;
			rVertex.f2Uv3 = pfTexcoords3 != nullptr ? XMFLOAT2(&pfTexcoords3[j * iTexcoordStride3]) : rVertex.f2Uv;
			rVertex.f2Uv4 = pfTexcoords4 != nullptr ? XMFLOAT2(&pfTexcoords4[j * iTexcoordStride4]) : rVertex.f2Uv;

			if (puiJoints != nullptr)
			{
				rVertex.fJoint = static_cast<float>(puiJoints[j * iJointsStride]);
				rVertex.f4Joint0 = XMFLOAT4(
					static_cast<float>(puiJoints[j * iJointsStride + 0]),
					static_cast<float>(puiJoints[j * iJointsStride + 1]),
					static_cast<float>(puiJoints[j * iJointsStride + 2]),
					static_cast<float>(puiJoints[j * iJointsStride + 3]));
				if (pfWeights != nullptr)
				{
					rVertex.f4Weight0 = XMFLOAT4(
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
				rVertex.fJoint = 0.0f;
				rVertex.f4Joint0 = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
				rVertex.f4Weight0 = XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f);
			}
		}

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
						rMaterial.indexBuffer.push_back(puiIndices[j] + static_cast<uint32_t>(iVertexStart));
					}
					break;
				}

				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
				{
					const uint16_t* puiIndices = static_cast<const uint16_t*>(pIndices);
					for (int64_t j = 0; j < static_cast<int64_t>(rIndicesAccessor.count); ++j)
					{
						rMaterial.indexBuffer.push_back(puiIndices[j] + static_cast<uint32_t>(iVertexStart));
					}
					break;
				}

				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
				{
					const uint8_t* puiIndices = static_cast<const uint8_t*>(pIndices);
					for (int64_t j = 0; j < static_cast<int64_t>(rIndicesAccessor.count); ++j)
					{
						rMaterial.indexBuffer.push_back(puiIndices[j] + static_cast<uint32_t>(iVertexStart));
					}
					break;
				}

				default:
					Assert(false);
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
std::unique_ptr<common::GltfSkeleton> BuildNodeSkeleton(const tinygltf::Model& rModel, std::unordered_map<int, int>& rNodeToJointMap)
{
	Log("BuildNodeSkeleton: Loading all nodes...");

	std::unordered_map<int, int> parentMap = BuildNodeParentMap(rModel);

	// Create skeleton with all nodes
	auto pSkeleton = std::make_unique<common::GltfSkeleton>();
	pSkeleton->uiNodeCount = static_cast<uint16_t>(rModel.nodes.size());
	Assert(pSkeleton->uiNodeCount <= common::GltfSkeleton::kiMaxNodes);

	// No skin joints for node-based animation
	pSkeleton->uiSkinJointCount = 0;

	// Build nodeToJointMap (identity mapping for node-based animation)
	rNodeToJointMap.clear();
	for (int64_t i = 0; i < static_cast<int64_t>(rModel.nodes.size()); ++i)
	{
		rNodeToJointMap[static_cast<int>(i)] = static_cast<int>(i);
	}

	Log("  Total nodes: {}", pSkeleton->uiNodeCount);

	// Process ALL nodes
	for (int64_t i = 0; i < static_cast<int64_t>(rModel.nodes.size()); ++i)
	{
		common::GltfNode& rNode = pSkeleton->nodes[i];
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
			// Loading column-major as row-major effectively reads the transpose, so transpose to get the correct matrix
			XMMATRIX matNode = XMMATRIX(
				static_cast<float>(rGltfNode.matrix[0]), static_cast<float>(rGltfNode.matrix[1]), static_cast<float>(rGltfNode.matrix[2]), static_cast<float>(rGltfNode.matrix[3]),
				static_cast<float>(rGltfNode.matrix[4]), static_cast<float>(rGltfNode.matrix[5]), static_cast<float>(rGltfNode.matrix[6]), static_cast<float>(rGltfNode.matrix[7]),
				static_cast<float>(rGltfNode.matrix[8]), static_cast<float>(rGltfNode.matrix[9]), static_cast<float>(rGltfNode.matrix[10]), static_cast<float>(rGltfNode.matrix[11]),
				static_cast<float>(rGltfNode.matrix[12]), static_cast<float>(rGltfNode.matrix[13]), static_cast<float>(rGltfNode.matrix[14]), static_cast<float>(rGltfNode.matrix[15]));
			XMStoreFloat4x4(&rNode.f4x4BindMatrix, XMMatrixTranspose(matNode));
		}
		else
		{
			XMStoreFloat4x4(&rNode.f4x4BindMatrix, XMMatrixIdentity());
		}
	}

	return pSkeleton;
}

std::unique_ptr<common::GltfSkeleton> LoadSkeleton(const tinygltf::Model& rModel, int32_t iSkinIndex)
{
	auto pSkeleton = std::make_unique<common::GltfSkeleton>();
	const tinygltf::Skin& rSkin = rModel.skins[iSkinIndex];

	// Store ALL nodes (not just skin joints)
	pSkeleton->uiNodeCount = static_cast<uint16_t>(rModel.nodes.size());
	Assert(pSkeleton->uiNodeCount <= common::GltfSkeleton::kiMaxNodes);

	// Build skin joint to node index mapping
	pSkeleton->uiSkinJointCount = static_cast<uint16_t>(rSkin.joints.size());
	Assert(pSkeleton->uiSkinJointCount <= common::GltfSkeleton::kiMaxSkinJoints);
	for (int64_t i = 0; i < static_cast<int64_t>(rSkin.joints.size()); ++i)
	{
		pSkeleton->skinJointToNode[i] = static_cast<uint16_t>(rSkin.joints[i]);
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
	for (int64_t i = 0; i < static_cast<int64_t>(rSkin.joints.size()); ++i)
	{
		if (pfInverseBindMatrices != nullptr)
		{
			// glTF stores matrices in column-major order, DirectXMath uses row-major
			// Loading column-major as row-major effectively reads the transpose, so transpose to get the correct matrix
			const float* pMatrix = &pfInverseBindMatrices[i * 16];
			XMMATRIX matInverseBind = XMMATRIX(pMatrix);
			XMStoreFloat4x4(&pSkeleton->inverseBindMatrices[i], XMMatrixTranspose(matInverseBind));
		}
		else
		{
			XMStoreFloat4x4(&pSkeleton->inverseBindMatrices[i], XMMatrixIdentity());
		}
	}

	// Process ALL nodes
	for (int64_t i = 0; i < static_cast<int64_t>(rModel.nodes.size()); ++i)
	{
		common::GltfNode& rNode = pSkeleton->nodes[i];
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
			// Loading column-major as row-major effectively reads the transpose, so transpose to get the correct matrix
			XMMATRIX matNode = XMMATRIX(
				static_cast<float>(rGltfNode.matrix[0]), static_cast<float>(rGltfNode.matrix[1]), static_cast<float>(rGltfNode.matrix[2]), static_cast<float>(rGltfNode.matrix[3]),
				static_cast<float>(rGltfNode.matrix[4]), static_cast<float>(rGltfNode.matrix[5]), static_cast<float>(rGltfNode.matrix[6]), static_cast<float>(rGltfNode.matrix[7]),
				static_cast<float>(rGltfNode.matrix[8]), static_cast<float>(rGltfNode.matrix[9]), static_cast<float>(rGltfNode.matrix[10]), static_cast<float>(rGltfNode.matrix[11]),
				static_cast<float>(rGltfNode.matrix[12]), static_cast<float>(rGltfNode.matrix[13]), static_cast<float>(rGltfNode.matrix[14]), static_cast<float>(rGltfNode.matrix[15]));
			XMStoreFloat4x4(&rNode.f4x4BindMatrix, XMMatrixTranspose(matNode));
		}
		else
		{
			XMStoreFloat4x4(&rNode.f4x4BindMatrix, XMMatrixIdentity());
		}
	}

	return pSkeleton;
}

void LoadAnimations(const tinygltf::Model& rModel,
	const std::unordered_map<int, int>& rNodeToNodeIndexMap,
	std::vector<common::GltfAnimation>& rAnimations,
	std::vector<common::GltfAnimationChannel>& rChannels,
	std::vector<common::GltfAnimationKeyframe>& rKeyframes)
{
	Log("LoadAnimations: nodeToNodeIndexMap has {} entries", rNodeToNodeIndexMap.size());

	for (const tinygltf::Animation& rAnim : rModel.animations)
	{
		common::GltfAnimation animation {};

		// Set name
		size_t iNameLength = std::min(rAnim.name.size(), static_cast<size_t>(common::GltfAnimation::kiMaxNameLength - 1));
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

			common::GltfAnimationChannel channel {};
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

			channel.uiKeyframeStart = static_cast<uint32_t>(rKeyframes.size());
			channel.uiKeyframeCount = static_cast<uint32_t>(rInputAccessor.count);

			int iValueStride = (channel.uiTargetPath == 1) ? 4 : 3; // Rotation is vec4, others vec3

			for (int64_t j = 0; j < static_cast<int64_t>(rInputAccessor.count); ++j)
			{
				common::GltfAnimationKeyframe keyframe {};
				keyframe.fTime = pfTimes[j];

				if (channel.uiInterpolation == 2) // CUBICSPLINE
				{
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
						float fW = (channel.uiTargetPath == 0) ? 0.0f : 1.0f; // Translation uses 0, Scale uses 1
						keyframe.f4InTangent = XMFLOAT4(pfValues[iBaseIdx + 0], pfValues[iBaseIdx + 1], pfValues[iBaseIdx + 2], 0.0f);
						keyframe.f4Value = XMFLOAT4(pfValues[iBaseIdx + iValueStride + 0], pfValues[iBaseIdx + iValueStride + 1], pfValues[iBaseIdx + iValueStride + 2], fW);
						keyframe.f4OutTangent = XMFLOAT4(pfValues[iBaseIdx + 2 * iValueStride + 0], pfValues[iBaseIdx + 2 * iValueStride + 1], pfValues[iBaseIdx + 2 * iValueStride + 2], 0.0f);
					}
				}
				else // STEP or LINEAR
				{
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
						keyframe.f4Value = XMFLOAT4(pfValues[j * iValueStride + 0], pfValues[j * iValueStride + 1], pfValues[j * iValueStride + 2], 1.0f);
					}
				}

				animation.fDuration = std::max(animation.fDuration, keyframe.fTime);

				rKeyframes.push_back(keyframe);
			}

			rChannels.push_back(channel);
			++animation.uiChannelCount;
		}

		if (animation.uiChannelCount > 0)
		{
			rAnimations.push_back(animation);
		}
	}
}

void ExportGltf::Export()
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

		Assert(gltfModel.textures.size() <= common::GltfHeader::kiMaxTextures);
		int64_t iTextureIndex = 0;
		Log("Pre-processing {} textures", gltfModel.textures.size());
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

			std::filesystem::path path = GetTextureIntermediatePath(rTexture.source, bOcclusion);
			const tinygltf::Image& rImage = gltfModel.images[rTexture.source];
			Texture texture(reinterpret_cast<const std::byte*>(rImage.image.data()), rImage.width, rImage.height, rImage.component);
			texture.MakeMipmaps(bOcclusion ? VK_FORMAT_BC4_UNORM_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK);

			texture.Save(path, bOcclusion ? VK_FORMAT_BC4_UNORM_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK, false);

			Log("  {}: Texture {} -> {}", iTextureIndex++, rImage.uri, path.filename().native());
		}

		std::filesystem::path path(mInputPath);
		path += ".GLTF_MODEL";
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
			std::unique_ptr<common::GltfSkeleton> pTempSkeleton = BuildNodeSkeleton(gltfModel, nodeToJointMap);
			Log("  Node-based animation detected: {} nodes in skeleton", pTempSkeleton->uiNodeCount);
		}

		const tinygltf::Scene& rScene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
		for (size_t i = 0; i < rScene.nodes.size(); ++i)
		{
			int iNodeIndex = rScene.nodes[i];
			const tinygltf::Node& rNode = gltfModel.nodes[iNodeIndex];
			Parent parent {nullptr, XMMatrixIdentity(), -1};
			LoadVertices(&parent, iNodeIndex, rNode, gltfModel, materials, nodeToJointMap, materialNodeInfos);
		}

		// Compute relative transforms for non-skinned materials and set jointCount
		std::vector<common::GltfMaterialInfo> materialInfos(gltfModel.materials.size());

		// Build parent map for node hierarchy traversal (used for both skeletal and node-based)
		std::unordered_map<int, int> nodeParentMap = BuildNodeParentMap(gltfModel);

		// Get skeleton joint count (for skinned materials)
		uint8_t uiSkeletonJointCount = 0;
		if (bUseSkeletalAnimation && gltfModel.skins.size() > 0)
		{
			uiSkeletonJointCount = static_cast<uint8_t>(gltfModel.skins[0].joints.size());
		}
		else if (bNodeBasedAnimation)
		{
			uiSkeletonJointCount = static_cast<uint8_t>(nodeToJointMap.size());
		}

		for (int64_t i = 0; i < static_cast<int64_t>(materialNodeInfos.size()); ++i)
		{
			MaterialNodeInfo& rInfo = materialNodeInfos[i];

			// Set jointCount: skinned materials have the skeleton's joint count, non-skinned have 0
			materialInfos[i].uiJointCount = rInfo.bHasSkinning ? uiSkeletonJointCount : 0;

			if (!rInfo.bHasSkinning && rInfo.iNodeIndex >= 0)
			{
				// Find nearest ancestor joint for this non-skinned material
				// Need to traverse node hierarchy to find joint ancestor
				int iCurrentNode = rInfo.iNodeIndex;
				int iAncestorJoint = -1;
				XMMATRIX matAncestorWorld = XMMatrixIdentity();

				// Traverse up to find ancestor joint
				while (iCurrentNode >= 0)
				{
					auto jointIt = nodeToJointMap.find(iCurrentNode);
					if (jointIt != nodeToJointMap.end())
					{
						iAncestorJoint = jointIt->second;
						if (bNodeBasedAnimation)
						{
							// For node-based animation, compute world transform of the joint node
							matAncestorWorld = ComputeNodeWorldTransform(iCurrentNode, gltfModel, nodeParentMap);
						}
						else
						{
							// For skeletal animation, compute joint's world transform from node hierarchy
							int iJointNodeIndex = gltfModel.skins[0].joints[iAncestorJoint];
							matAncestorWorld = ComputeNodeWorldTransform(iJointNodeIndex, gltfModel, nodeParentMap);
						}
						break;
					}
					auto parentIt = nodeParentMap.find(iCurrentNode);
					iCurrentNode = (parentIt != nodeParentMap.end()) ? parentIt->second : -1;
				}

				if (iAncestorJoint >= 0)
				{
					materialInfos[i].iParentNodeIndex = static_cast<int16_t>(iAncestorJoint);
					// Compute relative transform: meshBindWorld * inverse(nodeBindWorld)
					// In row-major: v * relativeTransform * nodeAnimated = v_animated
					XMMATRIX matRelative = rInfo.matMeshWorld * XMMatrixInverse(nullptr, matAncestorWorld);
					XMStoreFloat4x4(&materialInfos[i].f4x4RelativeTransform, matRelative);
					Log("  Material {}: non-skinned, parent node {}, node {}", i, iAncestorJoint, rInfo.iNodeIndex);
				}
			}
		}

		int64_t iMaterialVertexCount = 0;
		for (int64_t i = 0; i < static_cast<int64_t>(materials.size()); ++i)
		{
			tinygltf::Material& tinygltfMaterial = gltfModel.materials[i];
			Material& rMaterial = materials[i];
			Log("  {}: \"{}\", {} {} {} {} {} textures, {} vertices{}", i, tinygltfMaterial.name, tinygltfMaterial.pbrMetallicRoughness.baseColorTexture.index, tinygltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index, tinygltfMaterial.normalTexture.index, tinygltfMaterial.occlusionTexture.index, tinygltfMaterial.emissiveTexture.index, rMaterial.vertexBuffer.size(), materialInfos[i].iParentNodeIndex >= 0 ? " (non-skinned)" : "");
			iMaterialVertexCount += rMaterial.vertexBuffer.size();
		}

		std::vector<common::GltfVertex> vertices;
		vertices.reserve(iMaterialVertexCount);
	#if 0
		for (int64_t i = 0; i < static_cast<int64_t>(materials.size()); ++i)
		{
			std::vector<common::GltfVertex>& rMaterialVertexBuffer = materials[i].vertexBuffer;
			vertices.insert(vertices.end(), rMaterialVertexBuffer.begin(), rMaterialVertexBuffer.end());
		}
	#else
		for (int64_t i = 0; i < static_cast<int64_t>(materials.size()); ++i)
		{
			std::vector<common::GltfVertex>& rMaterialVertexBuffer = materials[i].vertexBuffer;
			Assert((materials[i].indexBuffer.size() % 3) == 0);

			for (uint32_t& ruiIndex : materials[i].indexBuffer)
			{
				common::GltfVertex& rMaterialGltfVertex = rMaterialVertexBuffer[ruiIndex];

				int64_t iFoundIndex = -1;
				for (int64_t j = 0; j < static_cast<int64_t>(vertices.size()); ++j)
				{
					common::GltfVertex& rGltfVertex = vertices[j];
					if (rGltfVertex == rMaterialGltfVertex)
					{
						iFoundIndex = j;
					}
				}

				if (iFoundIndex == -1)
				{
					vertices.push_back(rMaterialGltfVertex);
					iFoundIndex = vertices.size() - 1;
				}

				ruiIndex = static_cast<uint32_t>(iFoundIndex);
			}
		}
	#endif

		Log("{} material vertices -> {}", iMaterialVertexCount, vertices.size());

		std::map<float, int64_t> jointsMap;
		XMFLOAT3 f3Min = vertices[0].f3Pos;
		XMFLOAT3 f3Max = vertices[0].f3Pos;
		for (common::GltfVertex& rVertex : vertices)
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
			materialIndexPositions[i] = static_cast<uint32_t>(indices32.size());
			indices32.insert(indices32.end(), materials[i].indexBuffer.begin(), materials[i].indexBuffer.end());
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
		}

		{
			std::fstream fileStreamOut(preExportPath, std::ios::out | std::ios::binary);
			int64_t iVersion = GetVersion();
			fileStreamOut.write(reinterpret_cast<const char*>(&iVersion), sizeof(iVersion));
			fileStreamOut.flush();
			fileStreamOut.close();
		}

		Log("Samplers: {}", gltfModel.samplers.size());
		for (const tinygltf::Sampler& rSampler : gltfModel.samplers)
		{
			Log("  {} {} {} {}", ToVkFilter(rSampler.minFilter), ToVkFilter(rSampler.magFilter), ToVkSamplerAddressMode(rSampler.wrapS), ToVkSamplerAddressMode(rSampler.wrapT));
			Assert(ToVkSamplerAddressMode(rSampler.wrapS) == VK_SAMPLER_ADDRESS_MODE_REPEAT);
		}
	}

	auto [pHeader, dataSpan] = AllocateHeaderAndData(gltfModel.materials.size() * sizeof(common::GltfShaderData));
	common::GltfShaderData* pGltfShaderDatas = reinterpret_cast<common::GltfShaderData*>(dataSpan.data());

	Log("Textures: {}", gltfModel.textures.size());
	pHeader->gltfHeader.uiTextureCount = 0;
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
		pHeader->gltfHeader.pTextureCrcs[pHeader->gltfHeader.uiTextureCount++] = common::Crc(relativeFile.string());
	}

	Log("Materials: {}", gltfModel.materials.size());

	pHeader->gltfHeader.uiMaterialCount = static_cast<uint32_t>(gltfModel.materials.size());
	Assert(pHeader->gltfHeader.uiMaterialCount <= common::GltfHeader::kiMaxMaterials);

	size_t uiMaterialCount = 0;
	std::filesystem::path modelPath(mInputPath);
	modelPath += ".GLTF_MODEL";

	// Compute and store the model CRC in the header
	pHeader->gltfHeader.modelCrc = common::Crc(mRelativeFile + ".GLTF_MODEL");

	std::fstream fileStream(modelPath, std::ios::in | std::ios::binary);
	fileStream.read(reinterpret_cast<char*>(&uiMaterialCount), sizeof(uiMaterialCount));
	Assert(uiMaterialCount == pHeader->gltfHeader.uiMaterialCount);
	fileStream.read(reinterpret_cast<char*>(&pHeader->gltfHeader.puiIndexStarts[0]), uiMaterialCount * sizeof(uint32_t));

	// Read per-material skinning info for animation header
	std::vector<common::GltfMaterialInfo> materialInfos(uiMaterialCount);
	fileStream.read(reinterpret_cast<char*>(materialInfos.data()), uiMaterialCount * sizeof(common::GltfMaterialInfo));

	int64_t iMaterialIndex = 0;
	for (const tinygltf::Material& rMaterial : gltfModel.materials)
	{
		Log("  {}: {}", iMaterialIndex++, rMaterial.name);
		if (rMaterial.doubleSided == true)
		{
			Log("  Warning! Material is double sided");
		}

		common::GltfShaderData gltfShaderData {};

		if (rMaterial.values.find("baseColorFactor") != rMaterial.values.end())
		{
			double* pfData = rMaterial.values.at("baseColorFactor").ColorFactor().data();
			gltfShaderData.f4BaseColorFactor = XMFLOAT4(static_cast<float>(pfData[0]), static_cast<float>(pfData[1]), static_cast<float>(pfData[2]), static_cast<float>(pfData[3]));
		}

		gltfShaderData.f4EmissiveFactor = XMFLOAT4(static_cast<float>(rMaterial.emissiveFactor[0]), static_cast<float>(rMaterial.emissiveFactor[1]), static_cast<float>(rMaterial.emissiveFactor[2]), 1.0f);

		Assert(rMaterial.extensions.find("KHR_materials_pbrSpecularGlossiness") == rMaterial.extensions.end());
		gltfShaderData.fWorkflow = 0; // PBR_WORKFLOW_METALLIC_ROUGHNESS

		if (rMaterial.values.find("baseColorTexture") != rMaterial.values.end())
		{
			gltfShaderData.uiColorTextureIndex = static_cast<uint8_t>(rMaterial.values.at("baseColorTexture").TextureIndex());
			Log("  baseColorTexture: {}", gltfShaderData.uiColorTextureIndex);
			gltfShaderData.iColorTextureSet = rMaterial.values.at("baseColorTexture").TextureTexCoord();
		}

		if (rMaterial.values.find("metallicRoughnessTexture") != rMaterial.values.end())
		{
			gltfShaderData.uiPhysicalDescriptorTextureIndex = static_cast<uint8_t>(rMaterial.values.at("metallicRoughnessTexture").TextureIndex());
			Log("  metallicRoughnessTexture: {}", gltfShaderData.uiPhysicalDescriptorTextureIndex);
			gltfShaderData.iPhysicalDescriptorTextureSet = rMaterial.values.at("metallicRoughnessTexture").TextureTexCoord();
		}

		if (rMaterial.additionalValues.find("normalTexture") != rMaterial.additionalValues.end())
		{
			gltfShaderData.uiNormalTextureIndex = static_cast<uint8_t>(rMaterial.additionalValues.at("normalTexture").TextureIndex());
			Log("  normalTexture: {}", gltfShaderData.uiNormalTextureIndex);
			gltfShaderData.iNormalTextureSet = rMaterial.additionalValues.at("normalTexture").TextureTexCoord();
		}		

		if (rMaterial.additionalValues.find("occlusionTexture") != rMaterial.additionalValues.end())
		{
			gltfShaderData.uiOcclusionTextureIndex = static_cast<uint8_t>(rMaterial.additionalValues.at("occlusionTexture").TextureIndex());
			Log("  occlusionTexture: {}", gltfShaderData.uiOcclusionTextureIndex);
			gltfShaderData.iOcclusionTextureSet = rMaterial.additionalValues.at("occlusionTexture").TextureTexCoord();
		}

		if (rMaterial.additionalValues.find("emissiveTexture") != rMaterial.additionalValues.end())
		{
			gltfShaderData.uiEmissiveTextureIndex = static_cast<uint8_t>(rMaterial.additionalValues.at("emissiveTexture").TextureIndex());
			Log("  emissiveTexture: {}", gltfShaderData.uiEmissiveTextureIndex);
			gltfShaderData.iEmissiveTextureSet = rMaterial.additionalValues.at("emissiveTexture").TextureTexCoord();
		}

		if (rMaterial.values.find("metallicFactor") != rMaterial.values.end())
		{
			gltfShaderData.fMetallicFactor = static_cast<float>(rMaterial.values.at("metallicFactor").Factor());
		}

		if (rMaterial.values.find("roughnessFactor") != rMaterial.values.end())
		{
			gltfShaderData.fRoughnessFactor = static_cast<float>(rMaterial.values.at("roughnessFactor").Factor());
		}

		if (rMaterial.alphaMode != "OPAQUE")
		{
			Log("Warning: Material alphaMode is not OPAQUE (not yet supported in engine)");
		}
		Assert(rMaterial.alphaCutoff == 0.5f);
		if (rMaterial.additionalValues.find("alphaMode") != rMaterial.additionalValues.end())
		{
			Log("Warning: Found alphaMode in material (not yet supported in engine)");
		}
		gltfShaderData.fAlphaMask = 0; // ALPHAMODE_OPAQUE
		gltfShaderData.fAlphaMaskCutoff = static_cast<float>(rMaterial.alphaCutoff);

		*(pGltfShaderDatas++) = gltfShaderData;
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

		pHeader->gltfHeader.bHasAnimation = true;
		std::unique_ptr<common::GltfSkeleton> pSkeleton;
		std::unordered_map<int, int> nodeToJointMap;

		bool bUseSkeletalAnimation = DetermineAnimationPath(gltfModel);

		Log("  Using {} animation path", bUseSkeletalAnimation ? "SKELETAL" : "NODE-BASED");

		if (bUseSkeletalAnimation)
		{
			// Skeletal animation path
			Log("Loading skeletal animation...");
			pSkeleton = LoadSkeleton(gltfModel, 0);

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
			pSkeleton = BuildNodeSkeleton(gltfModel, nodeToJointMap);
		}

		Log("  {} nodes in skeleton, {} skin joints", pSkeleton->uiNodeCount, pSkeleton->uiSkinJointCount);

		// Load animations
		std::vector<common::GltfAnimation> animations;
		std::vector<common::GltfAnimationChannel> channels;
		std::vector<common::GltfAnimationKeyframe> keyframes;
		LoadAnimations(gltfModel, nodeToJointMap, animations, channels, keyframes);
		Log("  {} animations, {} channels, {} keyframes", animations.size(), channels.size(), keyframes.size());

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

		for (const common::GltfAnimation& rAnim : animations)
		{
			Log("    \"{}\": {} channels, {:.2f}s duration", rAnim.pcName, rAnim.uiChannelCount, rAnim.fDuration);
		}

		// Build animation header
		auto pAnimHeader = std::make_unique<common::GltfAnimationHeader>();
		pAnimHeader->uiAnimationCount = static_cast<uint32_t>(animations.size());
		pAnimHeader->uiChannelCount = static_cast<uint32_t>(channels.size());
		pAnimHeader->uiKeyframeCount = static_cast<uint32_t>(keyframes.size());
		pAnimHeader->skeleton = *pSkeleton;
		Assert(animations.size() <= common::GltfAnimationHeader::kiMaxAnimations);
		for (int64_t i = 0; i < static_cast<int64_t>(animations.size()); ++i)
		{
			pAnimHeader->animations[i] = animations[i];
		}

		// Copy per-material skinning info
		for (int64_t i = 0; i < static_cast<int64_t>(materialInfos.size()) && i < common::GltfHeader::kiMaxMaterials; ++i)
		{
			pAnimHeader->materialInfos[i] = materialInfos[i];
			if (materialInfos[i].iParentNodeIndex >= 0)
			{
				Log("  Material {}: non-skinned, parent node {}", i, materialInfos[i].iParentNodeIndex);
			}
		}

		// Append animation data to chunk buffer
		int64_t iAnimDataSize = sizeof(common::GltfAnimationHeader) +
			channels.size() * sizeof(common::GltfAnimationChannel) +
			keyframes.size() * sizeof(common::GltfAnimationKeyframe);

		int64_t iCurrentSize = static_cast<int64_t>(mHeaderAndData.size());
		int64_t iExpectedMaterialDataSize = gltfModel.materials.size() * sizeof(common::GltfShaderData);
		int64_t iExpectedOffset = common::RoundUp<int64_t, common::kiAlignmentBytes>(static_cast<int64_t>(sizeof(common::ChunkHeader))) + iExpectedMaterialDataSize;
		Log("  Animation data: writing at offset {} (buffer size {}), expected runtime offset {} (diff={})",
			iCurrentSize, mHeaderAndData.size(), iExpectedOffset, iCurrentSize - iExpectedOffset);
		mHeaderAndData.resize(iCurrentSize + iAnimDataSize);
		byte* pAnimData = mHeaderAndData.data() + iCurrentSize;

		std::memcpy(pAnimData, pAnimHeader.get(), sizeof(*pAnimHeader));
		pAnimData += sizeof(*pAnimHeader);

		std::memcpy(pAnimData, channels.data(), channels.size() * sizeof(common::GltfAnimationChannel));
		pAnimData += channels.size() * sizeof(common::GltfAnimationChannel);

		std::memcpy(pAnimData, keyframes.data(), keyframes.size() * sizeof(common::GltfAnimationKeyframe));
	}
}
