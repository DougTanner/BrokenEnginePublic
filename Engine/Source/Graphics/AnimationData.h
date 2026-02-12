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
	void EvaluateAnimation(int64_t iAnimationIndex, float fTime, uint32_t uiMaterialCount, common::MeshData* pMeshData, common::JointMatrix* pJointMatrices, int64_t iJointMatrixOffset) const;
	int64_t SkinnedMaterialCount(uint32_t uiMaterialCount) const;
	int64_t FindAnimation(std::string_view name) const;

	common::crc_t mCrc = 0;
	common::AnimationHeader mHeader {};

	// Pointers into eagerly-loaded pack memory (zero-copy)
	const common::ModelNode* mpNodes = nullptr;
	const uint16_t* mpSkinJointToNode = nullptr;
	const common::AnimationClip* mpAnimations = nullptr;
	const common::MaterialInfo* mpMaterialInfos = nullptr;
	const common::AnimationChannel* mpChannels = nullptr;
	const common::AnimationKeyframe* mpKeyframes = nullptr;
	const common::AnimationKeyframeCubic* mpCubicKeyframes = nullptr;

	// Pre-computed at load time (fixed upper-bound sizing)
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
