#include "SceneSkeletonLoader.h"

std::unordered_map<int, int> BuildNodeParentMap(const tinygltf::Model& rModel)
{
	std::unordered_map<int, int> parentMap;
	for (size_t i = 0; i < rModel.nodes.size(); ++i)
	{
		for (int iChildIndex : rModel.nodes[i].children)
		{
			parentMap.insert_or_assign(iChildIndex, static_cast<int>(i));
		}
	}
	return parentMap;
}

SkeletonData LoadSkeletonData(const tinygltf::Model& rModel)
{
	LOG(kDefault, kDebug, "LoadSkeletonData: Loading all nodes...");

	std::unordered_map<int, int> parentMap = BuildNodeParentMap(rModel);

	SkeletonData skeletonData;
	skeletonData.skeleton.uiNodeCount = static_cast<uint16_t>(rModel.nodes.size());
	ASSERT(skeletonData.skeleton.uiNodeCount <= common::Skeleton::kiMaxNodes);

	// Load skin joint data if a skin exists
	if (!rModel.skins.empty())
	{
		const tinygltf::Skin& rSkin = rModel.skins[0];
		skeletonData.skeleton.uiSkinJointCount = static_cast<uint16_t>(rSkin.joints.size());
		ASSERT(skeletonData.skeleton.uiSkinJointCount <= common::Skeleton::kiMaxSkinJoints);

		// Build skin joint to node index mapping
		skeletonData.skinJointToNode.resize(rSkin.joints.size());
		for (int64_t i = 0; i < static_cast<int64_t>(rSkin.joints.size()); ++i)
		{
			skeletonData.skinJointToNode.at(i) = static_cast<uint16_t>(rSkin.joints[i]);
		}

		// Load inverse bind matrices from accessor
		const float* pfInverseBindMatrices = nullptr;
		if (rSkin.inverseBindMatrices >= 0)
		{
			const tinygltf::Accessor& rAccessor = rModel.accessors[rSkin.inverseBindMatrices];
			ASSERT(rAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
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
				XMStoreFloat4x4(&skeletonData.inverseBindMatrices.at(i), matInverseBind);
			}
			else
			{
				XMStoreFloat4x4(&skeletonData.inverseBindMatrices.at(i), XMMatrixIdentity());
			}
		}

		LOG(kDefault, kDebug, "LoadSkeletonData: Loaded {} skin joints from skin", skeletonData.skeleton.uiSkinJointCount);
	}
	else
	{
		skeletonData.skeleton.uiSkinJointCount = 0;
	}

	LOG(kDefault, kDebug, "  Total nodes: {}", skeletonData.skeleton.uiNodeCount);

	// Process ALL nodes
	skeletonData.nodes.resize(rModel.nodes.size());
	for (int64_t i = 0; i < static_cast<int64_t>(rModel.nodes.size()); ++i)
	{
		common::ModelNode& rNode = skeletonData.nodes.at(i);
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
			XMMATRIX matNode = XMMATRIX(static_cast<float>(rGltfNode.matrix[0]), static_cast<float>(rGltfNode.matrix[1]), static_cast<float>(rGltfNode.matrix[2]), static_cast<float>(rGltfNode.matrix[3]), static_cast<float>(rGltfNode.matrix[4]), static_cast<float>(rGltfNode.matrix[5]), static_cast<float>(rGltfNode.matrix[6]), static_cast<float>(rGltfNode.matrix[7]), static_cast<float>(rGltfNode.matrix[8]), static_cast<float>(rGltfNode.matrix[9]), static_cast<float>(rGltfNode.matrix[10]), static_cast<float>(rGltfNode.matrix[11]), static_cast<float>(rGltfNode.matrix[12]), static_cast<float>(rGltfNode.matrix[13]), static_cast<float>(rGltfNode.matrix[14]), static_cast<float>(rGltfNode.matrix[15]));
			XMStoreFloat4x4(&rNode.f4x4BindMatrix, matNode);
		}
		else
		{
			XMStoreFloat4x4(&rNode.f4x4BindMatrix, XMMatrixIdentity());
		}
	}

	return skeletonData;
}
