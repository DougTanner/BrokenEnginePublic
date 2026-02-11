#pragma once

#include "DataFile.h"

namespace engine
{

class AnimationData
{
public:

	void Load(const std::byte* pAnimationData, common::crc_t crc);
	void EvaluateWorldMatrices(int64_t iAnimationIndex, float fTime, XMMATRIX* pWorldMatrices) const;
	void EvaluateMaterial(int64_t iMaterialIndex, const XMMATRIX* pWorldMatrices, common::MeshData* pMeshData, common::JointMatrix* pJointMatrices, int64_t iJointMatrixOffset) const;
	int64_t FindAnimation(std::string_view name) const;

	common::crc_t mCrc = 0;
	common::AnimationHeader mHeader {};
	std::vector<common::AnimationChannel> mChannels;
	std::vector<common::AnimationKeyframe> mKeyframes;

	// Pre-computed at load time
	XMMATRIX mBindPoseLocalMatrices[common::Skeleton::kiMaxNodes] {};
	bool mbAnimatedNodes[common::AnimationHeader::kiMaxAnimations][common::Skeleton::kiMaxNodes] {};
	XMMATRIX mAlignedInverseBindMatrices[common::Skeleton::kiMaxSkinJoints] {};
	XMMATRIX mAlignedRelativeTransforms[common::SceneHeader::kiMaxMaterials] {};

private:

	XMVECTOR InterpolateKeyframes(const common::AnimationChannel& rChannel, float fTime) const;
};

// Global registry by scene CRC
inline std::unordered_map<common::crc_t, AnimationData> gAnimationDataMap;

} // namespace engine
