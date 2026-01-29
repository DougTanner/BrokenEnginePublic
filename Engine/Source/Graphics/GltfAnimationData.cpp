#include "GltfAnimationData.h"

namespace engine
{

void GltfAnimationData::Load(const byte* pAnimationData)
{
	// Copy header
	std::memcpy(&mHeader, pAnimationData, sizeof(mHeader));
	pAnimationData += sizeof(mHeader);

	Log("GltfAnimationData::Load: uiAnimationCount={}, uiChannelCount={}, uiKeyframeCount={}, nodeCount={}, skinJointCount={}",
		mHeader.uiAnimationCount,
		mHeader.uiChannelCount,
		mHeader.uiKeyframeCount,
		mHeader.skeleton.uiNodeCount,
		mHeader.skeleton.uiSkinJointCount);

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

	// Handle edge cases outside the loop
	if (fTime <= pKeyframes[0].fTime)
	{
		uiKeyframe0 = 0;
		uiKeyframe1 = 0;
	}
	else if (fTime >= pKeyframes[uiKeyframeCount - 1].fTime)
	{
		uiKeyframe0 = uiKeyframeCount - 1;
		uiKeyframe1 = uiKeyframeCount - 1;
	}
	else
	{
		// Search for bracketing keyframes
		for (uint32_t i = 0; i < uiKeyframeCount - 1; ++i)
		{
			if (fTime >= pKeyframes[i].fTime && fTime < pKeyframes[i + 1].fTime)
			{
				uiKeyframe0 = i;
				uiKeyframe1 = i + 1;
				break;
			}
		}
	}

	const common::GltfAnimationKeyframe& rKey0 = pKeyframes[uiKeyframe0];
	const common::GltfAnimationKeyframe& rKey1 = pKeyframes[uiKeyframe1];

	// STEP interpolation
	if (rChannel.uiInterpolation == 0 || uiKeyframe0 == uiKeyframe1)
	{
		return XMLoadFloat4(&rKey0.f4Value);
	}

	float fDelta = rKey1.fTime - rKey0.fTime;
	float fT = (fTime - rKey0.fTime) / fDelta;

	// CUBICSPLINE interpolation (Hermite spline)
	// NOTE: This implementation follows the glTF 2.0 specification correctly.
	// The Vulkan-glTF-PBR reference implementation has bugs in cubicSplineInterpolation():
	// 1. Uses IN tangent (index A=0) for m0 instead of OUT tangent (should be index B=stride*2)
	// 2. Uses OUT tangent (index B) for m1 instead of IN tangent (should be index A)
	// 3. Line 650 uses m0 instead of m1 in the h11 term (copy-paste error)
	// Per glTF spec: m0 = OUT tangent of keyframe k, m1 = IN tangent of keyframe k+1
	if (rChannel.uiInterpolation == 2)
	{
		float fT2 = fT * fT;
		float fT3 = fT2 * fT;

		// Hermite basis functions
		float fH00 = 2.0f * fT3 - 3.0f * fT2 + 1.0f;  // p0 coefficient
		float fH10 = fT3 - 2.0f * fT2 + fT;           // m0 coefficient
		float fH01 = -2.0f * fT3 + 3.0f * fT2;        // p1 coefficient
		float fH11 = fT3 - fT2;                        // m1 coefficient

		XMVECTOR vecP0 = XMLoadFloat4(&rKey0.f4Value);
		XMVECTOR vecM0 = XMVectorScale(XMLoadFloat4(&rKey0.f4OutTangent), fDelta);  // OUT tangent of start keyframe (correct per glTF spec)
		XMVECTOR vecP1 = XMLoadFloat4(&rKey1.f4Value);
		XMVECTOR vecM1 = XMVectorScale(XMLoadFloat4(&rKey1.f4InTangent), fDelta);   // IN tangent of end keyframe (correct per glTF spec)

		XMVECTOR vecResult = XMVectorAdd(XMVectorAdd(XMVectorScale(vecP0, fH00), XMVectorScale(vecM0, fH10)), XMVectorAdd(XMVectorScale(vecP1, fH01), XMVectorScale(vecM1, fH11)));

		// Normalize quaternion results
		if (rChannel.uiTargetPath == 1)
		{
			vecResult = XMQuaternionNormalize(vecResult);
		}

		return vecResult;
	}

	// LINEAR interpolation
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

// Matrix convention: DirectXMath row-major to GLSL column-major
// Matrices are transposed before storage for correct GLSL interpretation
void GltfAnimationData::EvaluateWorldMatrices(int64_t iAnimationIndex, float fTime, XMMATRIX* pWorldMatrices) const
{
	const common::GltfSkeleton& rSkeleton = mHeader.skeleton;
	Assert(iAnimationIndex >= 0 && iAnimationIndex < mHeader.uiAnimationCount);
	const common::GltfAnimation& rAnimation = mHeader.animations[iAnimationIndex];

	// Use fixed-size arrays to avoid heap allocation per call
	XMVECTOR translations[common::GltfSkeleton::kiMaxNodes];
	XMVECTOR rotations[common::GltfSkeleton::kiMaxNodes];
	XMVECTOR scales[common::GltfSkeleton::kiMaxNodes];

	// Initialize node transforms from bind pose
	for (int64_t i = 0; i < rSkeleton.uiNodeCount; ++i)
	{
		const common::GltfNode& rNode = rSkeleton.nodes[i];
		translations[i] = XMLoadFloat4(&rNode.f4BindTranslation);
		rotations[i] = XMLoadFloat4(&rNode.f4BindRotation);
		scales[i] = XMLoadFloat4(&rNode.f4BindScale);
	}

	// Apply animation channels
	for (uint32_t i = rAnimation.uiChannelStart; i < rAnimation.uiChannelStart + rAnimation.uiChannelCount; ++i)
	{
		const common::GltfAnimationChannel& rChannel = mChannels[i];
		XMVECTOR vecValue = InterpolateKeyframes(rChannel, fTime);

		switch (rChannel.uiTargetPath)
		{
			case 0: // Translation
				translations[rChannel.uiNodeIndex] = vecValue;
				break;
			case 1: // Rotation
				rotations[rChannel.uiNodeIndex] = vecValue;
				break;
			case 2: // Scale
				scales[rChannel.uiNodeIndex] = vecValue;
				break;
		}
	}

	// Build local matrices and compute world matrices
	XMMATRIX localMatrices[common::GltfSkeleton::kiMaxNodes];

	for (int64_t i = 0; i < rSkeleton.uiNodeCount; ++i)
	{
		const common::GltfNode& rNode = rSkeleton.nodes[i];
		XMMATRIX matBindMatrix = XMLoadFloat4x4(&rNode.f4x4BindMatrix);
		XMMATRIX matScale = XMMatrixScalingFromVector(scales[i]);
		XMMATRIX matRotation = XMMatrixRotationQuaternion(rotations[i]);
		XMMATRIX matTranslation = XMMatrixTranslationFromVector(translations[i]);
		// Combine: matrix * S * R * T (matches Vulkan-glTF-PBR's T * R * S * M in GLM column-major)
		localMatrices[i] = matBindMatrix * matScale * matRotation * matTranslation;
	}

	for (int64_t i = 0; i < rSkeleton.uiNodeCount; ++i)
	{
		// Traverse up parent chain, composing local matrices (matches Vulkan-glTF-PBR)
		XMMATRIX matWorld = localMatrices[i];
		int16_t iParent = rSkeleton.nodes[i].iParentIndex;
		while (iParent >= 0)
		{
			matWorld = matWorld * localMatrices[iParent];
			iParent = rSkeleton.nodes[iParent].iParentIndex;
		}
		pWorldMatrices[i] = matWorld;
	}
}

void GltfAnimationData::Evaluate(int64_t iAnimationIndex, float fTime, int64_t iMaterialIndex, common::MeshShaderData* pMeshShaderData) const
{
	const common::GltfSkeleton& rSkeleton = mHeader.skeleton;
	const common::GltfMaterialInfo& rMaterialInfo = mHeader.materialInfos[iMaterialIndex];

	// Compute world matrices for all nodes
	XMMATRIX worldMatrices[common::GltfSkeleton::kiMaxNodes];
	EvaluateWorldMatrices(iAnimationIndex, fTime, worldMatrices);

	// Set joint count from material info (0 for non-skinned, >0 for skinned)
	pMeshShaderData->uiJointCount = rMaterialInfo.uiJointCount;

	if (rMaterialInfo.uiJointCount > 0)
	{
		// Skinned mesh: compute mesh world matrix from parent node if available
		XMMATRIX matMeshWorld = XMMatrixIdentity();
		if (rMaterialInfo.iParentNodeIndex >= 0)
		{
			// meshWorld = relativeTransform * nodeWorldAnimated
			XMMATRIX matRelative = XMLoadFloat4x4(&rMaterialInfo.f4x4RelativeTransform);
			matMeshWorld = matRelative * worldMatrices[rMaterialInfo.iParentNodeIndex];
		}

		// Store mesh world matrix (transposed for GLSL column-major format)
		XMStoreFloat4x4(&pMeshShaderData->matrix, XMMatrixTranspose(matMeshWorld));

		// Compute inverse of mesh world matrix (done at runtime, matching Vulkan-glTF-PBR)
		XMMATRIX matMeshWorldInverse = XMMatrixInverse(nullptr, matMeshWorld);

		// Compute joint matrices using skinJointToNode mapping: inverseBind * nodeWorld * inverse(meshWorld)
		// Order matches Vulkan-glTF-PBR: first inverseBind (world-bind -> joint-local),
		// then nodeWorld (joint-local -> animated-world), then inv(meshWorld) (to mesh-local for shader)
		for (int64_t i = 0; i < rSkeleton.uiSkinJointCount && i < common::MeshShaderData::kiMaxJoints; ++i)
		{
			uint16_t uiNodeIndex = rSkeleton.skinJointToNode[i];
			XMMATRIX matInverseBind = XMLoadFloat4x4(&rSkeleton.inverseBindMatrices[i]);
			XMMATRIX matJoint = matInverseBind * worldMatrices[uiNodeIndex] * matMeshWorldInverse;
			XMStoreFloat4x4(&pMeshShaderData->jointMatrix[i], XMMatrixTranspose(matJoint));
		}
	}
	else
	{
		// Non-skinned mesh: compute mesh matrix from parent node
		XMMATRIX matMeshWorld = XMMatrixIdentity();
		if (rMaterialInfo.iParentNodeIndex >= 0)
		{
			// meshWorld = relativeTransform * nodeWorldAnimated
			XMMATRIX matRelative = XMLoadFloat4x4(&rMaterialInfo.f4x4RelativeTransform);
			matMeshWorld = matRelative * worldMatrices[rMaterialInfo.iParentNodeIndex];
		}

		// Store mesh world matrix (transposed for GLSL column-major format)
		XMStoreFloat4x4(&pMeshShaderData->matrix, XMMatrixTranspose(matMeshWorld));

		// No joint matrices needed - jointCount == 0 signals shader to skip skinning
	}
}

} // namespace engine
