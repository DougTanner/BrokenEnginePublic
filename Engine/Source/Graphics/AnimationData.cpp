#include "AnimationData.h"
#include "ComparisonLog.h"
#include "ThreadLocal.h"

namespace engine
{

void AnimationData::Load(const std::byte* pAnimationData, common::crc_t crc)
{
	mCrc = crc;

	// Copy header
	std::memcpy(&mHeader, pAnimationData, sizeof(mHeader));
	pAnimationData += sizeof(mHeader);

	Log("AnimationData::Load: uiAnimationCount={}, uiChannelCount={}, uiKeyframeCount={}, nodeCount={}, skinJointCount={}", mHeader.uiAnimationCount, mHeader.uiChannelCount, mHeader.uiKeyframeCount, mHeader.skeleton.uiNodeCount, mHeader.skeleton.uiSkinJointCount);

	// Load channels
	mChannels.resize(mHeader.uiChannelCount);
	std::memcpy(mChannels.data(), pAnimationData, mHeader.uiChannelCount * sizeof(common::AnimationChannel));
	pAnimationData += mHeader.uiChannelCount * sizeof(common::AnimationChannel);

	// Load keyframes
	mKeyframes.resize(mHeader.uiKeyframeCount);
	std::memcpy(mKeyframes.data(), pAnimationData, mHeader.uiKeyframeCount * sizeof(common::AnimationKeyframe));

	// Verify topological order: every parent index must be less than the child index
	for (uint32_t i = 0; i < mHeader.skeleton.uiNodeCount; ++i)
	{
		ASSERT(mHeader.skeleton.nodes[i].iParentIndex < static_cast<int16_t>(i));
	}

	// Pre-compute bind-pose local matrices
	const common::Skeleton& rSkeletonPrecompute = mHeader.skeleton;
	for (uint32_t i = 0; i < rSkeletonPrecompute.uiNodeCount; ++i)
	{
		const common::ModelNode& rNode = rSkeletonPrecompute.nodes[i];
		XMMATRIX matBind = XMLoadFloat4x4(&rNode.f4x4BindMatrix);
		XMMATRIX matS = XMMatrixScalingFromVector(XMLoadFloat4(&rNode.f4BindScale));
		XMMATRIX matR = XMMatrixRotationQuaternion(XMLoadFloat4(&rNode.f4BindRotation));
		XMMATRIX matT = XMMatrixTranslationFromVector(XMLoadFloat4(&rNode.f4BindTranslation));
		mBindPoseLocalMatrices[i] = matBind * matS * matR * matT;
	}

	// Build per-animation animated node masks
	for (uint32_t iAnim = 0; iAnim < mHeader.uiAnimationCount; ++iAnim)
	{
		const common::AnimationClip& rClip = mHeader.animations[iAnim];
		for (uint32_t iCh = rClip.uiChannelStart; iCh < rClip.uiChannelStart + rClip.uiChannelCount; ++iCh)
		{
			mbAnimatedNodes[iAnim][mChannels[iCh].uiNodeIndex] = true;
		}
	}

	// Pre-load aligned inverse bind matrices
	for (uint32_t i = 0; i < rSkeletonPrecompute.uiSkinJointCount; ++i)
	{
		mAlignedInverseBindMatrices[i] = XMLoadFloat4x4(&rSkeletonPrecompute.inverseBindMatrices[i]);
	}

	// Pre-load aligned relative transforms
	for (int64_t i = 0; i < common::SceneHeader::kiMaxMaterials; ++i)
	{
		mAlignedRelativeTransforms[i] = XMLoadFloat4x4(&mHeader.materialInfos[i].f4x4RelativeTransform);
	}

	if (gbComparisonLoggingEnabled)
	{
		const common::Skeleton& rSkeleton = mHeader.skeleton;

		CompLog("\nSKELETON_LOAD:");
		CompLog("  node_count: %u", rSkeleton.uiNodeCount);
		CompLog("  skin_joint_count: %u", rSkeleton.uiSkinJointCount);
		CompLog("  skin_joint_to_node_mapping:");
		for (uint32_t i = 0; i < rSkeleton.uiSkinJointCount; ++i)
		{
			CompLog("    joint[%u] -> node[%u]", i, rSkeleton.skinJointToNode[i]);
		}

		// Log matrices transposed (columns as rows) to match GLM column-major format
		CompLog("  inverse_bind_matrices:");
		for (uint32_t i = 0; i < rSkeleton.uiSkinJointCount; ++i)
		{
			const DirectX::XMFLOAT4X4& rf4x4InverseBind = rSkeleton.inverseBindMatrices[i];
			CompLog("    inverse_bind[%u]: [%f, %f, %f, %f]", i, rf4x4InverseBind._11, rf4x4InverseBind._21, rf4x4InverseBind._31, rf4x4InverseBind._41);
			CompLog("                      [%f, %f, %f, %f]", rf4x4InverseBind._12, rf4x4InverseBind._22, rf4x4InverseBind._32, rf4x4InverseBind._42);
			CompLog("                      [%f, %f, %f, %f]", rf4x4InverseBind._13, rf4x4InverseBind._23, rf4x4InverseBind._33, rf4x4InverseBind._43);
			CompLog("                      [%f, %f, %f, %f]", rf4x4InverseBind._14, rf4x4InverseBind._24, rf4x4InverseBind._34, rf4x4InverseBind._44);
		}

		CompLog("  nodes:");
		for (uint32_t i = 0; i < rSkeleton.uiNodeCount; ++i)
		{
			const common::ModelNode& rNode = rSkeleton.nodes[i];
			CompLog("    node[%u]: parent=%d", i, rNode.iParentIndex);
			CompLog("      translation: (%f, %f, %f, %f)", rNode.f4BindTranslation.x, rNode.f4BindTranslation.y, rNode.f4BindTranslation.z, rNode.f4BindTranslation.w);
			CompLog("      rotation: (%f, %f, %f, %f)", rNode.f4BindRotation.x, rNode.f4BindRotation.y, rNode.f4BindRotation.z, rNode.f4BindRotation.w);
			CompLog("      scale: (%f, %f, %f, %f)", rNode.f4BindScale.x, rNode.f4BindScale.y, rNode.f4BindScale.z, rNode.f4BindScale.w);
		}

		CompLog("\nANIMATIONS_LOAD:");
		CompLog("  animation_count: %u", mHeader.uiAnimationCount);
		CompLog("  total_channel_count: %u", mHeader.uiChannelCount);
		CompLog("  total_keyframe_count: %u", mHeader.uiKeyframeCount);

		for (uint32_t i = 0; i < mHeader.uiAnimationCount; ++i)
		{
			const common::AnimationClip& rClip = mHeader.animations[i];
			CompLog("  animation[%u]:", i);
			CompLog("    name: \"%s\"", rClip.pcName);
			CompLog("    duration: %f", rClip.fDuration);
			CompLog("    channel_count: %u", rClip.uiChannelCount);

			uint32_t uiChannelEnd = std::min(rClip.uiChannelStart + 10u, rClip.uiChannelStart + rClip.uiChannelCount);
			for (uint32_t j = rClip.uiChannelStart; j < uiChannelEnd; ++j)
			{
				const common::AnimationChannel& rCh = mChannels[j];
				const char* pcPath = (rCh.uiTargetPath == 0) ? "translation" : (rCh.uiTargetPath == 1) ? "rotation" : "scale";
				const char* pcInterp = (rCh.uiInterpolation == 0) ? "STEP" : (rCh.uiInterpolation == 1) ? "LINEAR" : "CUBICSPLINE";
				CompLog("    channel[%u]:", j - rClip.uiChannelStart);
				CompLog("      node_index: %u", rCh.uiNodeIndex);
				CompLog("      target_path: %s (%u)", pcPath, rCh.uiTargetPath);
				CompLog("      interpolation: %s (%u)", pcInterp, rCh.uiInterpolation);
				CompLog("      keyframe_count: %u", rCh.uiKeyframeCount);
				if (rCh.uiKeyframeCount > 0)
				{
					const common::AnimationKeyframe& rKeyframe = mKeyframes[rCh.uiKeyframeStart];
					CompLog("      keyframe[0]: time=%f, value=(%f, %f, %f, %f)", rKeyframe.fTime, rKeyframe.f4Value.x, rKeyframe.f4Value.y, rKeyframe.f4Value.z, rKeyframe.f4Value.w);
				}
			}
		}
	}
}

int64_t AnimationData::FindAnimation(std::string_view name) const
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

XMVECTOR AnimationData::InterpolateKeyframes(const common::AnimationChannel& rChannel, float fTime) const
{
	const common::AnimationKeyframe* pKeyframes = &mKeyframes[rChannel.uiKeyframeStart];
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

	const common::AnimationKeyframe& rKey0 = pKeyframes[uiKeyframe0];
	const common::AnimationKeyframe& rKey1 = pKeyframes[uiKeyframe1];

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

// Matrix convention: DirectXMath row-major storage, GLSL column-major interpretation
// When GLSL reads row-major bytes as column-major mat4, it naturally receives the transpose,
// which converts row-vector convention (v*M) to column-vector convention (M*v)
void AnimationData::EvaluateWorldMatrices(int64_t iAnimationIndex, float fTime, XMMATRIX* pWorldMatrices) const
{
	const common::Skeleton& rSkeleton = mHeader.skeleton;
	ASSERT(iAnimationIndex >= 0 && iAnimationIndex < mHeader.uiAnimationCount && iAnimationIndex < common::AnimationHeader::kiMaxAnimations);
	const common::AnimationClip& rAnimation = mHeader.animations[iAnimationIndex];

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
	for (int64_t i = 0; i < rSkeleton.uiNodeCount; ++i)
	{
		if (pbAnimated[i])
		{
			const common::ModelNode& rNode = rSkeleton.nodes[i];
			pTranslations[i] = XMLoadFloat4(&rNode.f4BindTranslation);
			pRotations[i] = XMLoadFloat4(&rNode.f4BindRotation);
			pScales[i] = XMLoadFloat4(&rNode.f4BindScale);
		}
	}

	// Apply animation channels
	for (uint32_t i = rAnimation.uiChannelStart; i < rAnimation.uiChannelStart + rAnimation.uiChannelCount; ++i)
	{
		const common::AnimationChannel& rChannel = mChannels[i];
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
	for (int64_t i = 0; i < rSkeleton.uiNodeCount; ++i)
	{
		XMMATRIX matLocal;
		if (pbAnimated[i])
		{
			const common::ModelNode& rNode = rSkeleton.nodes[i];
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

		int16_t iParent = rSkeleton.nodes[i].iParentIndex;
		pWorldMatrices[i] = iParent >= 0 ? matLocal * pWorldMatrices[iParent] : matLocal;
	}

	common::gpThreadLocal->mWorkbuffer.Pop();
}

void AnimationData::EvaluateMaterial(int64_t iMaterialIndex, const XMMATRIX* pWorldMatrices, common::MeshData* pMeshData, common::JointMatrix* pJointMatrices, int64_t iJointMatrixOffset) const
{
	const common::Skeleton& rSkeleton = mHeader.skeleton;
	const common::MaterialInfo& rMaterialInfo = mHeader.materialInfos[iMaterialIndex];

	// Debug logging for free_cyberpunk_hovercar model only - use local static to avoid inline variable linkage issues
	static bool sbFirstFrameLogged = false;
	bool bIsComparisonModel = (mCrc == kFreeCyberpunkHovercarGltfCrc);
	bool bShouldLog = bIsComparisonModel && gComparisonLog.is_open() && !sbFirstFrameLogged;

	if (bShouldLog && iMaterialIndex == 0)
	{
		gComparisonLog << "\nEVALUATE_DEBUG: skinJointCount=" << rSkeleton.uiSkinJointCount
		               << ", iParentNodeIndex[1]=" << mHeader.materialInfos[1].iParentNodeIndex
		               << ", uiJointCount[1]=" << static_cast<int>(mHeader.materialInfos[1].uiJointCount)
		               << std::endl;

		// Log world matrix for mesh node of material 1
		int16_t iParentNode = mHeader.materialInfos[1].iParentNodeIndex;
		if (iParentNode >= 0)
		{
			DirectX::XMFLOAT4X4 f4x4World {};
			DirectX::XMStoreFloat4x4(&f4x4World, pWorldMatrices[iParentNode]);
			gComparisonLog << "worldMatrices[" << iParentNode << "] (mesh world for material 1):" << std::endl;
			gComparisonLog << "  [" << f4x4World._11 << ", " << f4x4World._12 << ", " << f4x4World._13 << ", " << f4x4World._14 << "]" << std::endl;
			gComparisonLog << "  [" << f4x4World._21 << ", " << f4x4World._22 << ", " << f4x4World._23 << ", " << f4x4World._24 << "]" << std::endl;
			gComparisonLog << "  [" << f4x4World._31 << ", " << f4x4World._32 << ", " << f4x4World._33 << ", " << f4x4World._34 << "]" << std::endl;
			gComparisonLog << "  [" << f4x4World._41 << ", " << f4x4World._42 << ", " << f4x4World._43 << ", " << f4x4World._44 << "]" << std::endl;
		}
	}

	// Log first frame evaluation for free_cyberpunk_hovercar
	if (bShouldLog && iMaterialIndex == 0)
	{
		CompLog("\nFIRST_FRAME_EVALUATION:");
		CompLog("  time: N/A (world matrices pre-computed)");

		CompLog("  world_matrices:");
		for (uint32_t i = 0; i < rSkeleton.uiNodeCount; ++i)
		{
			DirectX::XMFLOAT4X4 f4x4World {};
			DirectX::XMStoreFloat4x4(&f4x4World, pWorldMatrices[i]);
			CompLog("    world[%u]: [%f, %f, %f, %f]", i, f4x4World._11, f4x4World._21, f4x4World._31, f4x4World._41);
			CompLog("              [%f, %f, %f, %f]", f4x4World._12, f4x4World._22, f4x4World._32, f4x4World._42);
			CompLog("              [%f, %f, %f, %f]", f4x4World._13, f4x4World._23, f4x4World._33, f4x4World._43);
			CompLog("              [%f, %f, %f, %f]", f4x4World._14, f4x4World._24, f4x4World._34, f4x4World._44);
		}
	}

	// Set joint count from material info (0 for non-skinned, >0 for skinned), clamping to kiMaxJointsPerMesh
	pMeshData->uiJointCount = std::min(static_cast<uint32_t>(rMaterialInfo.uiJointCount), static_cast<uint32_t>(common::kiMaxJointsPerMesh));
	pMeshData->uiJointMatrixOffset = static_cast<uint32_t>(iJointMatrixOffset);

	// Debug logging for skinned materials
	if (bShouldLog && rMaterialInfo.uiJointCount > 0)
	{
		CompLog("\nDEBUG_MATERIAL[%lld]:", iMaterialIndex);
		CompLog("  iParentNodeIndex: %d", rMaterialInfo.iParentNodeIndex);
		CompLog("  uiJointCount: %u", rMaterialInfo.uiJointCount);
	}

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
		for (int64_t i = 0; i < rSkeleton.uiSkinJointCount && i < common::kiMaxJointsPerMesh; ++i)
		{
			uint16_t uiNodeIndex = rSkeleton.skinJointToNode[i];
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

		if (bShouldLog)
		{
			CompLog("\nMESH_DATA[%lld]:", iMaterialIndex);
			CompLog("  joint_count: %u", rMaterialInfo.uiJointCount);
			CompLog("  joint_matrix_offset: %lld", iJointMatrixOffset);
			CompLog("  mesh_world_matrix:");
			const DirectX::XMFLOAT4X4& rf4x4MeshWorld = pMeshData->matrix;
			CompLog("    [%f, %f, %f, %f]", rf4x4MeshWorld._11, rf4x4MeshWorld._12, rf4x4MeshWorld._13, rf4x4MeshWorld._14);
			CompLog("    [%f, %f, %f, %f]", rf4x4MeshWorld._21, rf4x4MeshWorld._22, rf4x4MeshWorld._23, rf4x4MeshWorld._24);
			CompLog("    [%f, %f, %f, %f]", rf4x4MeshWorld._31, rf4x4MeshWorld._32, rf4x4MeshWorld._33, rf4x4MeshWorld._34);
			CompLog("    [%f, %f, %f, %f]", rf4x4MeshWorld._41, rf4x4MeshWorld._42, rf4x4MeshWorld._43, rf4x4MeshWorld._44);

			CompLog("  joint_matrices:");
			for (uint32_t j = 0; j < std::min(static_cast<uint32_t>(rMaterialInfo.uiJointCount), 10u); ++j)
			{
				const common::JointMatrix& rJoint = pJointMatrices[iJointMatrixOffset + j];
				CompLog("    joint[%u]: [%f, %f, %f, %f]", j, rJoint.rows[0].x, rJoint.rows[0].y, rJoint.rows[0].z, rJoint.rows[0].w);
				CompLog("              [%f, %f, %f, %f]", rJoint.rows[1].x, rJoint.rows[1].y, rJoint.rows[1].z, rJoint.rows[1].w);
				CompLog("              [%f, %f, %f, %f]", rJoint.rows[2].x, rJoint.rows[2].y, rJoint.rows[2].z, rJoint.rows[2].w);
				CompLog("              [0.000000, 0.000000, 0.000000, 1.000000]");
			}

			// Close log after first skinned material
			sbFirstFrameLogged = true;
			CloseComparisonLog();
		}
	}
	else
	{
		// No joint matrices needed - jointCount == 0 signals shader to skip skinning

		if (bShouldLog)
		{
			CompLog("\nMESH_DATA[%lld]:", iMaterialIndex);
			CompLog("  joint_count: %u", rMaterialInfo.uiJointCount);
			CompLog("  mesh_world_matrix:");
			const DirectX::XMFLOAT4X4& rf4x4MeshWorld = pMeshData->matrix;
			CompLog("    [%f, %f, %f, %f]", rf4x4MeshWorld._11, rf4x4MeshWorld._12, rf4x4MeshWorld._13, rf4x4MeshWorld._14);
			CompLog("    [%f, %f, %f, %f]", rf4x4MeshWorld._21, rf4x4MeshWorld._22, rf4x4MeshWorld._23, rf4x4MeshWorld._24);
			CompLog("    [%f, %f, %f, %f]", rf4x4MeshWorld._31, rf4x4MeshWorld._32, rf4x4MeshWorld._33, rf4x4MeshWorld._34);
			CompLog("    [%f, %f, %f, %f]", rf4x4MeshWorld._41, rf4x4MeshWorld._42, rf4x4MeshWorld._43, rf4x4MeshWorld._44);
		}
	}
}

} // namespace engine
