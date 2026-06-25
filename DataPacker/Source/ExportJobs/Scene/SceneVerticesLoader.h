#pragma once

namespace tinygltf { class Model; class Node; struct Material; }

// Hash function for std::pair<int, int> to use with std::unordered_map
struct PairHash
{
	size_t operator()(const std::pair<int, int>& rPair) const
	{
		return std::hash<int>()(rPair.first) ^ (std::hash<int>()(rPair.second) << 1);
	}
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

XMMATRIX ComputeNodeWorldTransform(int iNodeIndex, const tinygltf::Model& rModel, const std::unordered_map<int, int>& rNodeParentMap);

// Per-scene state threaded through every recursive LoadVertices call.
// rMaterialNodeMap: tracks (originalMaterial, nodeIndex) -> effectiveMaterialIndex for handling primitives from different mesh nodes that share a material
struct LoadVerticesContext
{
	std::vector<common::ModelVertex>& rVertices;
	std::vector<Material>& rMaterials;
	std::vector<MaterialNodeInfo>& rMaterialNodeInfos;
	std::unordered_map<std::pair<int, int>, int, PairHash>& rMaterialNodeMap;
	bool bHasSkeleton;
};

void LoadVertices(Parent* pParent, int iCurrentNodeIndex, const tinygltf::Node& rNode, const tinygltf::Model& rModel, LoadVerticesContext& rContext);

bool IsOcclusion(int64_t iIndex, const tinygltf::Material& rMaterial);
bool IsNormal(int64_t iIndex, const tinygltf::Material& rMaterial);
