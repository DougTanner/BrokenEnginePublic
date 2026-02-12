#include "AnimationData.h"
#include "ThreadLocal.h"

namespace engine
{

void AnimationData::Load(const std::byte* pAnimationData, common::crc_t crc)
{
	mCrc = crc;

	// Copy header (now small - just counts + Skeleton counts)
	std::memcpy(&mHeader, pAnimationData, sizeof(mHeader));
	pAnimationData += sizeof(mHeader);

	Log("AnimationData::Load: uiAnimationCount={}, uiChannelCount={}, uiKeyframeCount={}, uiCubicKeyframeCount={}, nodeCount={}, skinJointCount={}", mHeader.uiAnimationCount, mHeader.uiChannelCount, mHeader.uiKeyframeCount, mHeader.uiCubicKeyframeCount, mHeader.skeleton.uiNodeCount, mHeader.skeleton.uiSkinJointCount);

	// Point into pack memory - no copying
	mpNodes = reinterpret_cast<const common::ModelNode*>(pAnimationData);
	pAnimationData += mHeader.skeleton.uiNodeCount * sizeof(common::ModelNode);

	mpSkinJointToNode = reinterpret_cast<const uint16_t*>(pAnimationData);
	pAnimationData += common::RoundUp<int64_t, 4>(mHeader.skeleton.uiSkinJointCount * static_cast<int64_t>(sizeof(uint16_t)));

	// Inverse bind matrices: read via pointer, pre-compute into aligned array
	const XMFLOAT4X4* pInverseBindMatrices = reinterpret_cast<const XMFLOAT4X4*>(pAnimationData);
	pAnimationData += mHeader.skeleton.uiSkinJointCount * sizeof(XMFLOAT4X4);

	mpAnimations = reinterpret_cast<const common::AnimationClip*>(pAnimationData);
	pAnimationData += mHeader.uiAnimationCount * sizeof(common::AnimationClip);

	mpMaterialInfos = reinterpret_cast<const common::MaterialInfo*>(pAnimationData);
	pAnimationData += mHeader.uiMaterialCount * sizeof(common::MaterialInfo);

	mpChannels = reinterpret_cast<const common::AnimationChannel*>(pAnimationData);
	pAnimationData += mHeader.uiChannelCount * sizeof(common::AnimationChannel);

	mpKeyframes = reinterpret_cast<const common::AnimationKeyframe*>(pAnimationData);
	pAnimationData += mHeader.uiKeyframeCount * sizeof(common::AnimationKeyframe);

	mpCubicKeyframes = reinterpret_cast<const common::AnimationKeyframeCubic*>(pAnimationData);

	// Verify topological order: every parent index must be less than the child index
	for (uint32_t i = 0; i < mHeader.skeleton.uiNodeCount; ++i)
	{
		ASSERT(mpNodes[i].iParentIndex < static_cast<int16_t>(i));
	}

	// Pre-compute bind-pose local matrices
	for (uint32_t i = 0; i < mHeader.skeleton.uiNodeCount; ++i)
	{
		const common::ModelNode& rNode = mpNodes[i];
		XMMATRIX matBind = XMLoadFloat4x4(&rNode.f4x4BindMatrix);
		XMMATRIX matS = XMMatrixScalingFromVector(XMLoadFloat4(&rNode.f4BindScale));
		XMMATRIX matR = XMMatrixRotationQuaternion(XMLoadFloat4(&rNode.f4BindRotation));
		XMMATRIX matT = XMMatrixTranslationFromVector(XMLoadFloat4(&rNode.f4BindTranslation));
		mBindPoseLocalMatrices[i] = matBind * matS * matR * matT;
	}

	// Build per-animation animated node masks
	for (uint32_t iAnim = 0; iAnim < mHeader.uiAnimationCount; ++iAnim)
	{
		const common::AnimationClip& rClip = mpAnimations[iAnim];
		for (uint32_t iCh = rClip.uiChannelStart; iCh < rClip.uiChannelStart + rClip.uiChannelCount; ++iCh)
		{
			mbAnimatedNodes[iAnim][mpChannels[iCh].uiNodeIndex] = true;
		}
	}

	// Pre-load aligned inverse bind matrices
	for (uint32_t i = 0; i < mHeader.skeleton.uiSkinJointCount; ++i)
	{
		mAlignedInverseBindMatrices[i] = XMLoadFloat4x4(&pInverseBindMatrices[i]);
	}

	// Pre-load aligned relative transforms
	for (uint32_t i = 0; i < mHeader.uiMaterialCount; ++i)
	{
		mAlignedRelativeTransforms[i] = XMLoadFloat4x4(&mpMaterialInfos[i].f4x4RelativeTransform);
	}
}

int64_t AnimationData::SkinnedMaterialCount(uint32_t uiMaterialCount) const
{
	int64_t iSkinnedMaterialCount = 0;
	for (uint32_t uiMaterialIndex = 0; uiMaterialIndex < uiMaterialCount; ++uiMaterialIndex)
	{
		if (mpMaterialInfos[uiMaterialIndex].uiJointCount > 0)
		{
			++iSkinnedMaterialCount;
		}
	}
	return iSkinnedMaterialCount;
}

void AnimationData::EvaluateAnimation(int64_t iAnimationIndex, float fTime, uint32_t uiMaterialCount, common::MeshData* pMeshData, common::JointMatrix* pJointMatrices, int64_t iJointMatrixOffset) const
{
	static constexpr int64_t kiMaxNodes = common::Skeleton::kiMaxNodes;
	XMMATRIX* pWorldMatrices = common::gpThreadLocal->mWorkbuffer.PushBuffer<XMMATRIX*>(kiMaxNodes * static_cast<int64_t>(sizeof(XMMATRIX)));
	EvaluateWorldMatrices(iAnimationIndex, fTime, pWorldMatrices);

	for (uint32_t uiMaterialIndex = 0; uiMaterialIndex < uiMaterialCount; ++uiMaterialIndex)
	{
		EvaluateMaterial(uiMaterialIndex, pWorldMatrices, pMeshData + uiMaterialIndex, pJointMatrices, iJointMatrixOffset);

		const common::MaterialInfo& rMaterialInfo = mpMaterialInfos[uiMaterialIndex];
		if (rMaterialInfo.uiJointCount > 0)
		{
			iJointMatrixOffset += mHeader.skeleton.uiSkinJointCount;
		}
	}

	common::gpThreadLocal->mWorkbuffer.Pop();
}

int64_t AnimationData::FindAnimation(std::string_view name) const
{
	for (int64_t i = 0; i < static_cast<int64_t>(mHeader.uiAnimationCount); ++i)
	{
		if (name == mpAnimations[i].pcName)
		{
			return i;
		}
	}
	return -1;
}

XMVECTOR AnimationData::InterpolateKeyframes(const common::AnimationChannel& rChannel, float fTime) const
{
	uint32_t uiKeyframeCount = rChannel.uiKeyframeCount;

	// CUBICSPLINE path: uses AnimationKeyframeCubic with tangent fields
	if (rChannel.uiInterpolation == 2)
	{
		const common::AnimationKeyframeCubic* pKeyframes = &mpCubicKeyframes[rChannel.uiKeyframeStart];

		uint32_t uiKeyframe0 = 0;
		uint32_t uiKeyframe1 = 0;

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
			// Binary search: find first keyframe with time > fTime, then step back one
			auto compare = [](float fT, const common::AnimationKeyframeCubic& rKey) { return fT < rKey.fTime; };
			const common::AnimationKeyframeCubic* pFound = std::upper_bound(pKeyframes, pKeyframes + uiKeyframeCount, fTime, compare);
			uint32_t uiUpper = static_cast<uint32_t>(pFound - pKeyframes);
			uiKeyframe0 = uiUpper > 0 ? uiUpper - 1 : 0;
			uiKeyframe1 = uiUpper < uiKeyframeCount ? uiUpper : uiKeyframeCount - 1;
		}

		const common::AnimationKeyframeCubic& rKey0 = pKeyframes[uiKeyframe0];
		const common::AnimationKeyframeCubic& rKey1 = pKeyframes[uiKeyframe1];

		if (uiKeyframe0 == uiKeyframe1)
		{
			return XMLoadFloat4(&rKey0.f4Value);
		}

		float fDelta = rKey1.fTime - rKey0.fTime;
		float fT = (fTime - rKey0.fTime) / fDelta;
		float fT2 = fT * fT;
		float fT3 = fT2 * fT;

		// Hermite basis functions
		// NOTE: This implementation follows the glTF 2.0 specification correctly.
		// The Vulkan-glTF-PBR reference implementation has bugs in cubicSplineInterpolation():
		// 1. Uses IN tangent (index A=0) for m0 instead of OUT tangent (should be index B=stride*2)
		// 2. Uses OUT tangent (index B) for m1 instead of IN tangent (should be index A)
		// 3. Line 650 uses m0 instead of m1 in the h11 term (copy-paste error)
		// Per glTF spec: m0 = OUT tangent of keyframe k, m1 = IN tangent of keyframe k+1
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

	// STEP/LINEAR path: uses compact AnimationKeyframe without tangent fields
	const common::AnimationKeyframe* pKeyframes = &mpKeyframes[rChannel.uiKeyframeStart];

	uint32_t uiKeyframe0 = 0;
	uint32_t uiKeyframe1 = 0;

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
		// Binary search: find first keyframe with time > fTime, then step back one
		auto compare = [](float fT, const common::AnimationKeyframe& rKey) { return fT < rKey.fTime; };
		const common::AnimationKeyframe* pFound = std::upper_bound(pKeyframes, pKeyframes + uiKeyframeCount, fTime, compare);
		uint32_t uiUpper = static_cast<uint32_t>(pFound - pKeyframes);
		uiKeyframe0 = uiUpper > 0 ? uiUpper - 1 : 0;
		uiKeyframe1 = uiUpper < uiKeyframeCount ? uiUpper : uiKeyframeCount - 1;
	}

	const common::AnimationKeyframe& rKey0 = pKeyframes[uiKeyframe0];
	const common::AnimationKeyframe& rKey1 = pKeyframes[uiKeyframe1];

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

// Matrix convention: DirectXMath row-major storage, GLSL column-major interpretation
// When GLSL reads row-major bytes as column-major mat4, it naturally receives the transpose,
// which converts row-vector convention (v*M) to column-vector convention (M*v)
void AnimationData::EvaluateWorldMatrices(int64_t iAnimationIndex, float fTime, XMMATRIX* pWorldMatrices) const
{
	ASSERT(iAnimationIndex >= 0 && iAnimationIndex < mHeader.uiAnimationCount && iAnimationIndex < common::AnimationHeader::kiMaxAnimations);
	const common::AnimationClip& rAnimation = mpAnimations[iAnimationIndex];

	// Allocate temporary TRS arrays from the thread-local workbuffer
	constexpr int64_t kiMaxNodes = common::Skeleton::kiMaxNodes;
	constexpr int64_t kiVecSize = kiMaxNodes * static_cast<int64_t>(sizeof(XMVECTOR));
	constexpr int64_t kiTotalSize = 3 * kiVecSize;

	std::byte* pBuffer = common::gpThreadLocal->mWorkbuffer.PushBuffer<std::byte*>(kiTotalSize);
	XMVECTOR* pTranslations = reinterpret_cast<XMVECTOR*>(pBuffer);
	XMVECTOR* pRotations    = reinterpret_cast<XMVECTOR*>(pBuffer + kiVecSize);
	XMVECTOR* pScales       = reinterpret_cast<XMVECTOR*>(pBuffer + 2 * kiVecSize);

	// Initialize node transforms from bind pose (only for animated nodes)
	const bool* pbAnimated = mbAnimatedNodes[iAnimationIndex];
	for (int64_t i = 0; i < mHeader.skeleton.uiNodeCount; ++i)
	{
		if (pbAnimated[i])
		{
			const common::ModelNode& rNode = mpNodes[i];
			pTranslations[i] = XMLoadFloat4(&rNode.f4BindTranslation);
			pRotations[i] = XMLoadFloat4(&rNode.f4BindRotation);
			pScales[i] = XMLoadFloat4(&rNode.f4BindScale);
		}
	}

	// Apply animation channels
	for (uint32_t i = rAnimation.uiChannelStart; i < rAnimation.uiChannelStart + rAnimation.uiChannelCount; ++i)
	{
		const common::AnimationChannel& rChannel = mpChannels[i];
		XMVECTOR vecValue = InterpolateKeyframes(rChannel, fTime);

		switch (rChannel.uiTargetPath)
		{
			case 0: // Translation
				pTranslations[rChannel.uiNodeIndex] = vecValue;
				break;
			case 1: // Rotation
				pRotations[rChannel.uiNodeIndex] = vecValue;
				break;
			case 2: // Scale
				pScales[rChannel.uiNodeIndex] = vecValue;
				break;
		}
	}

	// Build local matrices and compute world matrices in a single pass
	// Topological ordering (parent index < child index) guarantees parent world matrix is ready
	for (int64_t i = 0; i < mHeader.skeleton.uiNodeCount; ++i)
	{
		XMMATRIX matLocal;
		if (pbAnimated[i])
		{
			const common::ModelNode& rNode = mpNodes[i];
			XMMATRIX matBindMatrix = XMLoadFloat4x4(&rNode.f4x4BindMatrix);
			XMMATRIX matScale = XMMatrixScalingFromVector(pScales[i]);
			XMMATRIX matRotation = XMMatrixRotationQuaternion(pRotations[i]);
			XMMATRIX matTranslation = XMMatrixTranslationFromVector(pTranslations[i]);
			// Combine: matrix * S * R * T (matches Vulkan-glTF-PBR's T * R * S * M in GLM column-major)
			matLocal = matBindMatrix * matScale * matRotation * matTranslation;
		}
		else
		{
			matLocal = mBindPoseLocalMatrices[i];
		}

		int16_t iParent = mpNodes[i].iParentIndex;
		pWorldMatrices[i] = iParent >= 0 ? matLocal * pWorldMatrices[iParent] : matLocal;
	}

	common::gpThreadLocal->mWorkbuffer.Pop();
}

void AnimationData::EvaluateMaterial(int64_t iMaterialIndex, const XMMATRIX* pWorldMatrices, common::MeshData* pMeshData, common::JointMatrix* pJointMatrices, int64_t iJointMatrixOffset) const
{
	const common::MaterialInfo& rMaterialInfo = mpMaterialInfos[iMaterialIndex];

	// Set joint count from material info (0 for non-skinned, >0 for skinned), clamping to kiMaxJointsPerMesh
	pMeshData->uiJointCount = std::min(static_cast<uint32_t>(rMaterialInfo.uiJointCount), static_cast<uint32_t>(common::kiMaxJointsPerMesh));
	pMeshData->uiJointMatrixOffset = static_cast<uint32_t>(iJointMatrixOffset);

	// Compute mesh world matrix from parent node
	XMMATRIX matMeshWorld = XMMatrixIdentity();
	if (rMaterialInfo.iParentNodeIndex >= 0)
	{
		// meshWorld = relativeTransform * nodeWorldAnimated
		XMMATRIX matRelative = mAlignedRelativeTransforms[iMaterialIndex];
		matMeshWorld = matRelative * pWorldMatrices[rMaterialInfo.iParentNodeIndex];
	}

	// Store mesh world matrix - NO explicit transpose needed
	// Row-major (DirectXMath) to column-major (GLSL) storage reinterpretation naturally transposes
	// This gives GLSL the correct matrix for column-vector multiplication (mat * vec)
	XMStoreFloat4x4(&pMeshData->matrix, matMeshWorld);

	// Compute normal matrix: transpose(inverse(mat3(meshWorld)))
	XMMATRIX matNormal = XMMatrixTranspose(XMMatrixInverse(nullptr, matMeshWorld));
	XMStoreFloat4(&pMeshData->normalMatrix[0], matNormal.r[0]);
	XMStoreFloat4(&pMeshData->normalMatrix[1], matNormal.r[1]);
	XMStoreFloat4(&pMeshData->normalMatrix[2], matNormal.r[2]);

	if (rMaterialInfo.uiJointCount > 0)
	{
		// Compute inverse of mesh world matrix (done at runtime, matching Vulkan-glTF-PBR)
		XMMATRIX matMeshWorldInverse = XMMatrixInverse(nullptr, matMeshWorld);

		// Compute joint matrices: inverseBind * nodeWorld * inv(meshWorld)
		// Write to separate joint matrix buffer at the specified offset
		// NO explicit transpose - storage conversion handles row-major to column-major
		for (int64_t i = 0; i < mHeader.skeleton.uiSkinJointCount && i < common::kiMaxJointsPerMesh; ++i)
		{
			uint16_t uiNodeIndex = mpSkinJointToNode[i];
			XMMATRIX matInverseBind = mAlignedInverseBindMatrices[i];
			XMMATRIX matJoint = matInverseBind * pWorldMatrices[uiNodeIndex] * matMeshWorldInverse;
			// Store 3 rows with translation packed into .w components:
			// r[0].w=0 -> Tx, r[1].w=0 -> Ty, r[2].w=0 -> Tz
			XMStoreFloat4(&pJointMatrices[iJointMatrixOffset + i].rows[0], matJoint.r[0]);
			XMStoreFloat4(&pJointMatrices[iJointMatrixOffset + i].rows[1], matJoint.r[1]);
			XMStoreFloat4(&pJointMatrices[iJointMatrixOffset + i].rows[2], matJoint.r[2]);
			XMFLOAT4 f4Translation {};
			XMStoreFloat4(&f4Translation, matJoint.r[3]);
			pJointMatrices[iJointMatrixOffset + i].rows[0].w = f4Translation.x;
			pJointMatrices[iJointMatrixOffset + i].rows[1].w = f4Translation.y;
			pJointMatrices[iJointMatrixOffset + i].rows[2].w = f4Translation.z;
		}
	}
}

} // namespace engine
