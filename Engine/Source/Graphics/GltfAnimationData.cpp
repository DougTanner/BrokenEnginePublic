#include "GltfAnimationData.h"

namespace engine
{

void GltfAnimationData::Load(const byte* pAnimationData)
{
	// Copy header
	std::memcpy(&mHeader, pAnimationData, sizeof(mHeader));
	pAnimationData += sizeof(mHeader);

	// Load channels
	mChannels.resize(mHeader.uiChannelCount);
	std::memcpy(mChannels.data(), pAnimationData, mHeader.uiChannelCount * sizeof(common::GltfAnimationChannel));
	pAnimationData += mHeader.uiChannelCount * sizeof(common::GltfAnimationChannel);

	// Load keyframes
	mKeyframes.resize(mHeader.uiKeyframeCount);
	std::memcpy(mKeyframes.data(), pAnimationData, mHeader.uiKeyframeCount * sizeof(common::GltfAnimationKeyframe));
}

int64_t GltfAnimationData::FindAnimation(std::string_view name) const
{
	for (int64_t i = 0; i < static_cast<int64_t>(mHeader.uiAnimationCount); ++i)
	{
		if (name == mHeader.animations[i].pcName)
		{
			return i;
		}
	}
	return -1;
}

XMVECTOR GltfAnimationData::InterpolateKeyframes(const common::GltfAnimationChannel& rChannel, float fTime) const
{
	const common::GltfAnimationKeyframe* pKeyframes = &mKeyframes[rChannel.uiKeyframeStart];
	uint32_t uiKeyframeCount = rChannel.uiKeyframeCount;

	// Find the two keyframes to interpolate between
	uint32_t uiKeyframe0 = 0;
	uint32_t uiKeyframe1 = 0;
	for (uint32_t i = 0; i < uiKeyframeCount - 1; ++i)
	{
		if (fTime >= pKeyframes[i].fTime && fTime < pKeyframes[i + 1].fTime)
		{
			uiKeyframe0 = i;
			uiKeyframe1 = i + 1;
			break;
		}
		if (fTime >= pKeyframes[uiKeyframeCount - 1].fTime)
		{
			uiKeyframe0 = uiKeyframeCount - 1;
			uiKeyframe1 = uiKeyframeCount - 1;
		}
	}

	const common::GltfAnimationKeyframe& rKey0 = pKeyframes[uiKeyframe0];
	const common::GltfAnimationKeyframe& rKey1 = pKeyframes[uiKeyframe1];

	// STEP interpolation
	if (rChannel.uiInterpolation == 0 || uiKeyframe0 == uiKeyframe1)
	{
		return XMLoadFloat4(&rKey0.f4Value);
	}

	// LINEAR interpolation
	float fDelta = rKey1.fTime - rKey0.fTime;
	float fT = (fTime - rKey0.fTime) / fDelta;

	XMVECTOR vec0 = XMLoadFloat4(&rKey0.f4Value);
	XMVECTOR vec1 = XMLoadFloat4(&rKey1.f4Value);

	// Use slerp for rotations (quaternions)
	if (rChannel.uiTargetPath == 1)
	{
		return XMQuaternionSlerp(vec0, vec1, fT);
	}

	// Lerp for translation and scale
	return XMVectorLerp(vec0, vec1, fT);
}

void GltfAnimationData::Evaluate(int64_t iAnimationIndex, float fTime, XMMATRIX* pJointMatrices) const
{
	const common::GltfSkeleton& rSkeleton = mHeader.skeleton;
	const common::GltfAnimation& rAnimation = mHeader.animations[iAnimationIndex];

	// Use fixed-size arrays to avoid heap allocation per call
	XMVECTOR translations[common::GltfSkeleton::kiMaxJoints];
	XMVECTOR rotations[common::GltfSkeleton::kiMaxJoints];
	XMVECTOR scales[common::GltfSkeleton::kiMaxJoints];

	// Initialize joint transforms from bind pose
	for (int64_t i = 0; i < rSkeleton.uiJointCount; ++i)
	{
		const common::GltfJoint& rJoint = rSkeleton.joints[i];
		translations[i] = XMLoadFloat4(&rJoint.f4BindTranslation);
		rotations[i] = XMLoadFloat4(&rJoint.f4BindRotation);
		scales[i] = XMLoadFloat4(&rJoint.f4BindScale);
	}

	// Apply animation channels
	for (uint32_t i = rAnimation.uiChannelStart; i < rAnimation.uiChannelStart + rAnimation.uiChannelCount; ++i)
	{
		const common::GltfAnimationChannel& rChannel = mChannels[i];
		XMVECTOR vecValue = InterpolateKeyframes(rChannel, fTime);

		switch (rChannel.uiTargetPath)
		{
			case 0: // Translation
				translations[rChannel.uiJointIndex] = vecValue;
				break;
			case 1: // Rotation
				rotations[rChannel.uiJointIndex] = vecValue;
				break;
			case 2: // Scale
				scales[rChannel.uiJointIndex] = vecValue;
				break;
		}
	}

	// Build local matrices and compute world matrices
	XMMATRIX localMatrices[common::GltfSkeleton::kiMaxJoints];
	XMMATRIX worldMatrices[common::GltfSkeleton::kiMaxJoints];

	for (int64_t i = 0; i < rSkeleton.uiJointCount; ++i)
	{
		XMMATRIX matScale = XMMatrixScalingFromVector(scales[i]);
		XMMATRIX matRotation = XMMatrixRotationQuaternion(rotations[i]);
		XMMATRIX matTranslation = XMMatrixTranslationFromVector(translations[i]);
		localMatrices[i] = matScale * matRotation * matTranslation;
	}

	for (int64_t i = 0; i < rSkeleton.uiJointCount; ++i)
	{
		const common::GltfJoint& rJoint = rSkeleton.joints[i];
		if (rJoint.iParentIndex < 0)
		{
			worldMatrices[i] = localMatrices[i];
		}
		else
		{
			worldMatrices[i] = localMatrices[i] * worldMatrices[rJoint.iParentIndex];
		}
	}

	// Multiply by inverse bind matrices to get final joint matrices
	for (int64_t i = 0; i < rSkeleton.uiJointCount; ++i)
	{
		XMMATRIX matInverseBind = XMLoadFloat4x4(&rSkeleton.joints[i].f4x4InverseBindMatrix);
		pJointMatrices[i] = matInverseBind * worldMatrices[i];
	}
}

} // namespace engine
