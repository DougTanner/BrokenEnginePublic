#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class AnimationData
{
public:

	void Load(const std::byte* pAnimationData, int64_t iAnimationBytes, common::crc_t crc);
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

	// Pre-computed at load time, runtime-sized to the header counts (see Load)
	common::AlignedUniquePtr<XMMATRIX> mpBindPoseLocalMatrices;       // uiNodeCount entries
	std::vector<uint8_t> mbAnimatedNodes;                             // uiAnimationCount * uiNodeCount, row stride uiNodeCount
	common::AlignedUniquePtr<XMMATRIX> mpAlignedInverseBindMatrices;  // uiSkinJointCount entries
	common::AlignedUniquePtr<XMMATRIX> mpAlignedRelativeTransforms;   // uiMaterialCount entries

private:

	XMVECTOR InterpolateKeyframes(const common::AnimationChannel& rChannel, float fTime) const;
};

// Global registry by scene CRC. mpAnimations[].fDuration feeds the per-entity animation clock in sim-phase
// code (Players/Spaceships Update, inside BT_CLIENT guards). That clock is deliberately excluded from
// SharedCrcMembers — promoting the animation-time field into
// the shared CRC would couple determinism to client-only pack data.
inline std::unordered_map<common::crc_t, AnimationData> gAnimationDataMap;

void LoadAnimationDataFromEagerChunks();

} // namespace engine

#endif // defined(BT_CLIENT)
