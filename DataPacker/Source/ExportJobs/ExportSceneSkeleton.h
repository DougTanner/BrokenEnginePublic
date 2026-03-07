#pragma once

#include "tinygltf/tiny_gltf.h"

struct SkeletonData
{
	common::Skeleton skeleton;  // counts only
	std::vector<common::ModelNode> nodes;
	std::vector<uint16_t> skinJointToNode;
	std::vector<XMFLOAT4X4> inverseBindMatrices;
};

std::unordered_map<int, int> BuildNodeParentMap(const tinygltf::Model& rModel);

// Build a skeleton from node hierarchy for models with node-based animation (no skin)
// This now stores ALL nodes, consistent with the skeletal animation path
SkeletonData BuildNodeSkeleton(const tinygltf::Model& rModel, std::unordered_map<int, int>& rNodeToJointMap);

SkeletonData LoadSkeleton(const tinygltf::Model& rModel, int32_t iSkinIndex);
