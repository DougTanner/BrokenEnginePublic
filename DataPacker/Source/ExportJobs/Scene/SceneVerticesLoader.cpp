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

void LoadVertices(Parent* pParent, int iCurrentNodeIndex, const tinygltf::Node& rNode, const tinygltf::Model& rModel, LoadVerticesContext& rContext)
{
	std::vector<common::ModelVertex>& rVertices = rContext.rVertices;
	std::vector<Material>& rMaterials = rContext.rMaterials;
	std::vector<MaterialNodeInfo>& rMaterialNodeInfos = rContext.rMaterialNodeInfos;
	std::unordered_map<std::pair<int, int>, int, PairHash>& rMaterialNodeMap = rContext.rMaterialNodeMap;
	const std::unordered_map<int, int>& rNodeToJointMap = rContext.rNodeToJointMap;

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
		uint32_t uiVertexStart = static_cast<uint32_t>(rVertices.size());
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
			rMaterialNodeMap.insert_or_assign(key, iEffectiveMaterial);
			rMaterialNodeInfos.at(iOriginalMaterial).iNodeIndex = iCurrentNodeIndex;
			rMaterialNodeInfos.at(iOriginalMaterial).matMeshWorld = matNode * matLocal;
			rMaterialNodeInfos.at(iOriginalMaterial).iOriginalMaterialIndex = iOriginalMaterial;
		}
		else if (rMaterialNodeInfos.at(iOriginalMaterial).iNodeIndex == iCurrentNodeIndex)
		{
			// Original material already used by this same node - use it
			iEffectiveMaterial = iOriginalMaterial;
			rMaterialNodeMap.insert_or_assign(key, iEffectiveMaterial);
		}
		else
		{
			// Original material used by a different node - create a new split material
			iEffectiveMaterial = static_cast<int>(rMaterials.size());
			rMaterialNodeMap.insert_or_assign(key, iEffectiveMaterial);
			rMaterials.emplace_back();
			MaterialNodeInfo newInfo;
			newInfo.bHasSkinning = bHasSkinning;
			newInfo.iNodeIndex = iCurrentNodeIndex;
			newInfo.matMeshWorld = matNode * matLocal;
			newInfo.iOriginalMaterialIndex = iOriginalMaterial;
			rMaterialNodeInfos.push_back(newInfo);
			LOG(kDefault, kVerbose, "  Split material {} for node {} -> new material {}", iOriginalMaterial, iCurrentNodeIndex, iEffectiveMaterial);
		}

		Material& rMaterial = rMaterials.at(iEffectiveMaterial);
		MaterialNodeInfo& rMaterialNodeInfo = rMaterialNodeInfos.at(iEffectiveMaterial);

		if (bHasSkinning)
		{
			rMaterialNodeInfo.bHasSkinning = true;
		}

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

		// De-duplicate vertices using hash map for O(1) lookups
		std::unordered_map<common::ModelVertex, uint32_t> vertexToIndex;
		std::vector<uint32_t> indexRemap(rPositionAccessor.count);
		rVertices.reserve(rVertices.size() + rPositionAccessor.count);
		for (int64_t j = 0; j < static_cast<int64_t>(rPositionAccessor.count); ++j)
		{
			common::ModelVertex vertex;

			auto vecPosition = XMVectorSet(pfPositions[j * iPositionStride + 0], pfPositions[j * iPositionStride + 1], pfPositions[j * iPositionStride + 2], 1.0f);
			auto vecNormal = pfNormals != nullptr ? XMVectorSet(pfNormals[j * iNormalStride], pfNormals[j * iNormalStride + 1], pfNormals[j * iNormalStride + 2], 0.0f) : XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

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

		uint32_t uiNewVertexCount = static_cast<uint32_t>(rVertices.size()) - uiVertexStart;
		LOG(kDefault, kVerbose, "  Vertices: {} -> {} (deduplicated {})", rPositionAccessor.count, uiNewVertexCount, rPositionAccessor.count - uiNewVertexCount);

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
