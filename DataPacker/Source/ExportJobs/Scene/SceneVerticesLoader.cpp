#include "SceneVerticesLoader.h"

namespace
{

// Fetches a primitive vertex attribute as a typed pointer + element stride (in units of T).
// Returns {nullptr, 0} when the attribute is absent. Stride falls back to the accessor's component
// count when the buffer view is tightly packed.
template <typename T>
std::pair<const T*, int> FindAttribute(const tinygltf::Primitive& rPrimitive, const tinygltf::Model& rModel, const char* pcAttributeName)
{
	auto it = rPrimitive.attributes.find(pcAttributeName);
	if (it == rPrimitive.attributes.end())
	{
		return {nullptr, 0};
	}

	const tinygltf::Accessor& rAccessor = rModel.accessors[it->second];
	if constexpr (std::is_same_v<T, float>)
	{
		ASSERT(rAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
	}
	const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
	const T* pData = reinterpret_cast<const T*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
	int iStride = rAccessor.ByteStride(rBufferView) != 0 ? static_cast<int>(rAccessor.ByteStride(rBufferView) / sizeof(T)) : tinygltf::GetNumComponentsInType(rAccessor.type);
	return {pData, iStride};
}

// Composes a glTF node's local transform: either the TRS triple (scale * rotation * translation) or
// the explicit 16-element matrix (glTF stores column-major; loading as row-major puts translation in
// row 3, which is correct for DirectXMath). Identity when the node carries neither. Shared by
// ComputeNodeWorldTransform and LoadVertices so the two TRS/matrix branches cannot drift.
XMMATRIX NodeLocalMatrix(const tinygltf::Node& rNode)
{
	bool bHasTRS = rNode.translation.size() == 3 || rNode.rotation.size() == 4 || rNode.scale.size() == 3;
	bool bHasMatrix = rNode.matrix.size() == 16;

	if (bHasTRS)
	{
		XMVECTOR vecTranslation = rNode.translation.size() == 3 ? XMVectorSet(static_cast<float>(rNode.translation[0]), static_cast<float>(rNode.translation[1]), static_cast<float>(rNode.translation[2]), 0.0f) : XMVectorZero();
		XMVECTOR vecRotation = rNode.rotation.size() == 4 ? XMVectorSet(static_cast<float>(rNode.rotation[0]), static_cast<float>(rNode.rotation[1]), static_cast<float>(rNode.rotation[2]), static_cast<float>(rNode.rotation[3])) : XMQuaternionIdentity();
		XMVECTOR vecScale = rNode.scale.size() == 3 ? XMVectorSet(static_cast<float>(rNode.scale[0]), static_cast<float>(rNode.scale[1]), static_cast<float>(rNode.scale[2]), 1.0f) : XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);

		return XMMatrixScalingFromVector(vecScale) * XMMatrixRotationQuaternion(vecRotation) * XMMatrixTranslationFromVector(vecTranslation);
	}

	if (bHasMatrix)
	{
		// glTF stores matrices in column-major order, DirectXMath uses row-major
		// Loading column-major data as row-major puts translation into row 3, which is correct for DirectXMath
		return XMMATRIX(
			static_cast<float>(rNode.matrix[0]), static_cast<float>(rNode.matrix[1]), static_cast<float>(rNode.matrix[2]), static_cast<float>(rNode.matrix[3]),
			static_cast<float>(rNode.matrix[4]), static_cast<float>(rNode.matrix[5]), static_cast<float>(rNode.matrix[6]), static_cast<float>(rNode.matrix[7]),
			static_cast<float>(rNode.matrix[8]), static_cast<float>(rNode.matrix[9]), static_cast<float>(rNode.matrix[10]), static_cast<float>(rNode.matrix[11]),
			static_cast<float>(rNode.matrix[12]), static_cast<float>(rNode.matrix[13]), static_cast<float>(rNode.matrix[14]), static_cast<float>(rNode.matrix[15]));
	}

	return XMMatrixIdentity();
}

// Resolves the effective material index for a primitive's (originalMaterial, nodeIndex) pair. Primitives
// from different mesh nodes that share a glTF material need separate entries so each carries its own mesh
// world transform at runtime; this caches the mapping in rContext and mints split materials as needed.
int ResolveEffectiveMaterial(LoadVerticesContext& rContext, int iOriginalMaterial, int iCurrentNodeIndex, bool bHasSkinning, const XMMATRIX& rMatMeshWorld)
{
	std::vector<Material>& rMaterials = rContext.rMaterials;
	std::vector<MaterialNodeInfo>& rMaterialNodeInfos = rContext.rMaterialNodeInfos;
	std::unordered_map<std::pair<int, int>, int, PairHash>& rMaterialNodeMap = rContext.rMaterialNodeMap;

	std::pair<int, int> key = std::make_pair(iOriginalMaterial, iCurrentNodeIndex);
	auto it = rMaterialNodeMap.find(key);

	if (it != rMaterialNodeMap.end())
	{
		// Already have a material entry for this (originalMaterial, nodeIndex) combination
		return it->second;
	}

	if (rMaterialNodeInfos.at(iOriginalMaterial).iNodeIndex < 0)
	{
		// Original material not yet used - use it directly
		rMaterialNodeMap.insert_or_assign(key, iOriginalMaterial);
		rMaterialNodeInfos.at(iOriginalMaterial).iNodeIndex = iCurrentNodeIndex;
		rMaterialNodeInfos.at(iOriginalMaterial).matMeshWorld = rMatMeshWorld;
		rMaterialNodeInfos.at(iOriginalMaterial).iOriginalMaterialIndex = iOriginalMaterial;
		return iOriginalMaterial;
	}

	if (rMaterialNodeInfos.at(iOriginalMaterial).iNodeIndex == iCurrentNodeIndex)
	{
		// Original material already used by this same node - use it
		rMaterialNodeMap.insert_or_assign(key, iOriginalMaterial);
		return iOriginalMaterial;
	}

	// Original material used by a different node - create a new split material
	int iEffectiveMaterial = static_cast<int>(rMaterials.size());
	rMaterialNodeMap.insert_or_assign(key, iEffectiveMaterial);
	rMaterials.emplace_back();
	MaterialNodeInfo newInfo;
	newInfo.bHasSkinning = bHasSkinning;
	newInfo.iNodeIndex = iCurrentNodeIndex;
	newInfo.matMeshWorld = rMatMeshWorld;
	newInfo.iOriginalMaterialIndex = iOriginalMaterial;
	rMaterialNodeInfos.push_back(newInfo);
	LOG(kDefault, kVerbose, "  Split material {} for node {} -> new material {}", iOriginalMaterial, iCurrentNodeIndex, iEffectiveMaterial);
	return iEffectiveMaterial;
}

// Assembles deduplicated vertices for one primitive, appending unique vertices to rVertices and filling
// rIndexRemap (original-vertex -> deduped-index). Static, non-skeleton meshes are baked to world space;
// skinned / skeletal vertices stay in mesh-local space for the runtime skinning pipeline.
void BuildVertices(std::vector<common::ModelVertex>& rVertices, std::vector<uint32_t>& rIndexRemap, const tinygltf::Primitive& rPrimitive, const tinygltf::Model& rModel, bool bHasSkinning, bool bHasSkeleton, const XMMATRIX& rMatMeshWorld)
{
	// Position (required) - accessor also supplies the vertex count that drives the loop below
	const tinygltf::Accessor& rPositionAccessor = rModel.accessors[rPrimitive.attributes.find("POSITION")->second];
	auto [pfPositions, iPositionStride] = FindAttribute<float>(rPrimitive, rModel, "POSITION");

	auto [pfNormals, iNormalStride] = FindAttribute<float>(rPrimitive, rModel, "NORMAL");
	auto [pfTexcoords0, iTexcoordStride0] = FindAttribute<float>(rPrimitive, rModel, "TEXCOORD_0");
	auto [pfTexcoords1, iTexcoordStride1] = FindAttribute<float>(rPrimitive, rModel, "TEXCOORD_1");
	auto [pfTexcoords2, iTexcoordStride2] = FindAttribute<float>(rPrimitive, rModel, "TEXCOORD_2");
	auto [pfTexcoords3, iTexcoordStride3] = FindAttribute<float>(rPrimitive, rModel, "TEXCOORD_3");
	auto [pfTexcoords4, iTexcoordStride4] = FindAttribute<float>(rPrimitive, rModel, "TEXCOORD_4");
	auto [pfWeights, iWeightsStride] = FindAttribute<float>(rPrimitive, rModel, "WEIGHTS_0");

	// Joints are read as uint16_t; the runtime skinning path requires that component type
	auto [puiJoints, iJointsStride] = FindAttribute<uint16_t>(rPrimitive, rModel, "JOINTS_0");
	ASSERT(puiJoints == nullptr || rModel.accessors[rPrimitive.attributes.at("JOINTS_0")].componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);

	uint32_t uiVertexStart = static_cast<uint32_t>(rVertices.size());

	// Build raw per-primitive vertices into a temp buffer, then dedup via meshoptimizer's byte-identity
	// remap (replaces the hand-rolled unordered_map<ModelVertex> dedup). ModelVertex is static_assert
	// padding-free (sizeof == 100), so the raw-byte compare has no uninitialized-padding hazard.
	std::vector<common::ModelVertex> rawVertices(rPositionAccessor.count);
	for (int64_t j = 0; j < static_cast<int64_t>(rPositionAccessor.count); ++j)
	{
		common::ModelVertex& rVertex = rawVertices.at(j);

		auto vecPosition = XMVectorSet(pfPositions[j * iPositionStride + 0], pfPositions[j * iPositionStride + 1], pfPositions[j * iPositionStride + 2], 1.0f);
		auto vecNormal = pfNormals != nullptr ? XMVectorSet(pfNormals[j * iNormalStride], pfNormals[j * iNormalStride + 1], pfNormals[j * iNormalStride + 2], 0.0f) : XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

		if (!bHasSkinning && !bHasSkeleton)
		{
			// Only transform static models without skeleton data
			// Skinned vertices must remain in local space for runtime skinning pipeline
			vecPosition = XMVector4Transform(vecPosition, rMatMeshWorld);
			vecNormal = XMVector3TransformNormal(vecNormal, rMatMeshWorld);
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

	// Generate a byte-identity dedup remap over the raw vertices (NULL indices => identity 0..count-1
	// sequence), append the unique vertices to the shared scene-wide buffer at uiVertexStart, then
	// translate each original-vertex slot to its final scene index for AppendIndices.
	std::vector<uint32_t> remap(rPositionAccessor.count);
	size_t uiUniqueCount = meshopt_generateVertexRemap(remap.data(), nullptr, rPositionAccessor.count, rawVertices.data(), rPositionAccessor.count, sizeof(common::ModelVertex));

	rVertices.resize(uiVertexStart + uiUniqueCount);
	meshopt_remapVertexBuffer(rVertices.data() + uiVertexStart, rawVertices.data(), rPositionAccessor.count, sizeof(common::ModelVertex), remap.data());

	rIndexRemap.resize(rPositionAccessor.count);
	for (int64_t j = 0; j < static_cast<int64_t>(rPositionAccessor.count); ++j)
	{
		rIndexRemap.at(j) = uiVertexStart + remap.at(j);
	}

	LOG(kDefault, kVerbose, "  Vertices: {} -> {} (deduplicated {})", rPositionAccessor.count, uiUniqueCount, rPositionAccessor.count - uiUniqueCount);
}

// Appends a primitive's indices (remapped through rIndexRemap to deduped vertex indices) onto rIndexBuffer,
// upconverting the glTF 8/16/32-bit index component type to uint32_t.
void AppendIndices(std::vector<uint32_t>& rIndexBuffer, const tinygltf::Primitive& rPrimitive, const tinygltf::Model& rModel, const std::vector<uint32_t>& rIndexRemap)
{
	if (rPrimitive.indices <= -1)
	{
		return;
	}

	const tinygltf::Accessor& rIndicesAccessor = rModel.accessors[rPrimitive.indices];
	const tinygltf::BufferView& rIndiciesBufferView = rModel.bufferViews[rIndicesAccessor.bufferView];
	const tinygltf::Buffer& rIndiciesBuffer = rModel.buffers[rIndiciesBufferView.buffer];
	const void* pIndices = &(rIndiciesBuffer.data[rIndicesAccessor.byteOffset + rIndiciesBufferView.byteOffset]);

	rIndexBuffer.reserve(rIndexBuffer.size() + rIndicesAccessor.count);
	switch (rIndicesAccessor.componentType)
	{
		case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
		{
			const uint32_t* puiIndices = static_cast<const uint32_t*>(pIndices);
			for (int64_t j = 0; j < static_cast<int64_t>(rIndicesAccessor.count); ++j)
			{
				rIndexBuffer.push_back(rIndexRemap.at(puiIndices[j]));
			}
			break;
		}

		case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
		{
			const uint16_t* puiIndices = static_cast<const uint16_t*>(pIndices);
			for (int64_t j = 0; j < static_cast<int64_t>(rIndicesAccessor.count); ++j)
			{
				rIndexBuffer.push_back(rIndexRemap.at(puiIndices[j]));
			}
			break;
		}

		case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
		{
			const uint8_t* puiIndices = static_cast<const uint8_t*>(pIndices);
			for (int64_t j = 0; j < static_cast<int64_t>(rIndicesAccessor.count); ++j)
			{
				rIndexBuffer.push_back(rIndexRemap.at(puiIndices[j]));
			}
			break;
		}

		default:
			ASSERT(false);
	}
}

}

XMMATRIX ComputeNodeWorldTransform(int iNodeIndex, const tinygltf::Model& rModel, const std::unordered_map<int, int>& rNodeParentMap)
{
	XMMATRIX matWorld = XMMatrixIdentity();
	int iCurrent = iNodeIndex;

	// Build chain from node to root, then multiply in reverse
	std::vector<XMMATRIX> chain;
	while (iCurrent >= 0)
	{
		const tinygltf::Node& rNode = rModel.nodes[iCurrent];
		XMMATRIX matLocal = NodeLocalMatrix(rNode);

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

void LoadVertices(Parent* pParent, int iCurrentNodeIndex, const tinygltf::Node& rNode, const tinygltf::Model& rModel, LoadVerticesContext& rContext)
{
	std::vector<common::ModelVertex>& rVertices = rContext.rVertices;
	std::vector<Material>& rMaterials = rContext.rMaterials;
	std::vector<MaterialNodeInfo>& rMaterialNodeInfos = rContext.rMaterialNodeInfos;

	XMMATRIX matNode = NodeLocalMatrix(rNode);

	for (size_t i = 0; i < rNode.children.size(); ++i)
	{
		Parent parent {pParent, matNode, iCurrentNodeIndex};
		LoadVertices(&parent, rNode.children[i], rModel.nodes[rNode.children[i]], rModel, rContext);
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

	XMMATRIX matMeshWorld = matNode * matLocal;

	const tinygltf::Mesh& rMesh = rModel.meshes[rNode.mesh];
	for (size_t i = 0; i < rMesh.primitives.size(); ++i)
	{
		const tinygltf::Primitive& rPrimitive = rMesh.primitives[i];
		ASSERT(rPrimitive.attributes.find("POSITION") != rPrimitive.attributes.end());
		ASSERT(rPrimitive.attributes.find("COLOR_0") == rPrimitive.attributes.end());
		if (rPrimitive.attributes.find("TEXCOORD_6") != rPrimitive.attributes.end())
		{
			LOG(kDefault, kWarning, "WARNING: Found TEXCOORD_6");
		}

		ASSERT(rPrimitive.material >= 0);
		int iOriginalMaterial = rPrimitive.material;
		bool bHasSkinning = rPrimitive.attributes.find("JOINTS_0") != rPrimitive.attributes.end();

		int iEffectiveMaterial = ResolveEffectiveMaterial(rContext, iOriginalMaterial, iCurrentNodeIndex, bHasSkinning, matMeshWorld);
		Material& rMaterial = rMaterials.at(iEffectiveMaterial);
		MaterialNodeInfo& rMaterialNodeInfo = rMaterialNodeInfos.at(iEffectiveMaterial);

		if (bHasSkinning)
		{
			rMaterialNodeInfo.bHasSkinning = true;
		}

		std::vector<uint32_t> indexRemap;
		BuildVertices(rVertices, indexRemap, rPrimitive, rModel, bHasSkinning, rContext.bHasSkeleton, matMeshWorld);

		AppendIndices(rMaterial.indexBuffer, rPrimitive, rModel, indexRemap);
	}
}

namespace
{

bool UsesTexture(const tinygltf::ParameterMap& rParameters, const char* pcTextureName, int64_t iIndex)
{
	tinygltf::ParameterMap::const_iterator iterator = rParameters.find(pcTextureName);
	return iterator != rParameters.end() && iterator->second.TextureIndex() == iIndex;
}

} // namespace

bool IsOcclusion(int64_t iIndex, const tinygltf::Material& rMaterial)
{
	bool bOcclusion = UsesTexture(rMaterial.additionalValues, "occlusionTexture", iIndex);

	// Check if occlusion texture is shared with another texture type (if so, treat as non-occlusion)
	if (bOcclusion && (UsesTexture(rMaterial.values, "baseColorTexture", iIndex) || UsesTexture(rMaterial.additionalValues, "normalTexture", iIndex) || UsesTexture(rMaterial.values, "metallicRoughnessTexture", iIndex) || UsesTexture(rMaterial.additionalValues, "emissiveTexture", iIndex)))
	{
		bOcclusion = false;
	}

	return bOcclusion;
}

bool IsNormal(int64_t iIndex, const tinygltf::Material& rMaterial)
{
	return UsesTexture(rMaterial.additionalValues, "normalTexture", iIndex);
}
