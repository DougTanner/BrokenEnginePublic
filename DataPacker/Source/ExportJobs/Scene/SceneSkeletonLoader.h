#pragma once

namespace tinygltf { class Model; }

struct SkeletonData
{
	common::Skeleton skeleton;  // counts only
	std::vector<common::ModelNode> nodes;
	std::vector<uint16_t> skinJointToNode;
	std::vector<XMFLOAT4X4> inverseBindMatrices;
};

std::unordered_map<int, int> BuildNodeParentMap(const tinygltf::Model& rModel);

// Load skeleton from all nodes, conditionally loading skin data if skins exist
SkeletonData LoadSkeletonData(const tinygltf::Model& rModel);
