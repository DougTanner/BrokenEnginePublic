#if defined(BT_CLIENT)

#include "AnimationData.h"

namespace engine
{

namespace
{

template <typename KEYFRAME>
void FindKeyframePair(const KEYFRAME* pKeyframes, uint32_t uiKeyframeCount, float fTime, uint32_t& ruiKeyframe0, uint32_t& ruiKeyframe1)
{
	if (fTime <= pKeyframes[0].fTime)
	{
		ruiKeyframe0 = 0;
		ruiKeyframe1 = 0;
		return;
	}
	if (fTime >= pKeyframes[uiKeyframeCount - 1].fTime)
	{
		ruiKeyframe0 = uiKeyframeCount - 1;
		ruiKeyframe1 = uiKeyframeCount - 1;
		return;
	}

	// Find first keyframe after fTime, then pair it with the preceding keyframe.
	const KEYFRAME* pFound = std::upper_bound(pKeyframes, pKeyframes + uiKeyframeCount, fTime, [](float fValue, const KEYFRAME& rKeyframe)
	{
		return fValue < rKeyframe.fTime;
	});
	uint32_t uiUpperIndex = static_cast<uint32_t>(pFound - pKeyframes);
	ruiKeyframe0 = uiUpperIndex > 0 ? uiUpperIndex - 1 : 0;
	ruiKeyframe1 = uiUpperIndex < uiKeyframeCount ? uiUpperIndex : uiKeyframeCount - 1;
}

} // namespace

void AnimationData::Load(const std::byte* pAnimationData, int64_t iAnimationBytes, common::crc_t crc)
{
	mCrc = crc;

	// Trust boundary (byte extent): bound the header read and every alias-pointer advance below against the region's
	// true byte extent. iAnimationBytes comes from the authoritative chunk table (EagerChunk::iDataSize =
	// ChunkLocation::uiSize - kiChunkDataOffset, minus the scene-array prefix), so a count that is <= its structural
	// max but larger than the chunk's actual animation bytes is rejected instead of walking off the eager pack buffer.
	if (iAnimationBytes < static_cast<int64_t>(sizeof(mHeader)))
	{
		throw common::CorruptStreamException("AnimationData::Load");
	}

	// Copy header (now small - just counts + Skeleton counts)
	std::memcpy(&mHeader, pAnimationData, sizeof(mHeader));
	pAnimationData += sizeof(mHeader);

	// Trust boundary (counts): these counts come from on-disk pack bytes and drive reinterpret_cast pointer advances
	// over the chunk plus the runtime sizing of the pre-computed member containers (mpBindPoseLocalMatrices etc.).
	// A corrupt/tampered count would request an absurd allocation or walk the alias pointers off the eager pack
	// buffer, so reject implausible counts against the structural maxima before any of them is used. Channels/keyframes
	// have no container of their own (they only advance alias pointers); bound them by a generous absolute ceiling
	// — the glTF-driven producer has no clean structural max, and the ceiling caps the pointer arithmetic to a sane
	// range. (A <=-max-but-oversized count vs the chunk's actual bytes is caught by the iAnimationBytes byte-extent
	// bound applied to each advance below — the region's true extent is sourced from the chunk table, not from the
	// scene chunk's ChunkHeader::iSize, which excludes the appended animation section.)
	if (mHeader.skeleton.uiNodeCount > common::Skeleton::kiMaxNodes
		|| mHeader.skeleton.uiSkinJointCount > common::Skeleton::kiMaxSkinJoints
		|| mHeader.uiAnimationCount > common::AnimationHeader::kiMaxAnimations
		|| mHeader.uiAnimationCount == 0  // chunk loaded only when bHasAnimation, so 0 is corrupt; EvaluateWorldMatrices indexes [0, uiAnimationCount)
		|| mHeader.uiMaterialCount > common::SceneHeader::kiMaxMaterials
		|| mHeader.uiChannelCount > common::kiMaxDeserializedCapacity
		|| mHeader.uiKeyframeCount > common::kiMaxDeserializedCapacity
		|| mHeader.uiCubicKeyframeCount > common::kiMaxDeserializedCapacity)
	{
		throw common::CorruptStreamException("AnimationData::Load");
	}

	LOG(kLoading, kDebug, "AnimationData::Load: animations {}, channels {}, keyframes {}, cubicKeyframes {}, nodes {}, skinJoints {}", mHeader.uiAnimationCount, mHeader.uiChannelCount, mHeader.uiKeyframeCount, mHeader.uiCubicKeyframeCount, mHeader.skeleton.uiNodeCount, mHeader.skeleton.uiSkinJointCount);

	// Byte-extent bound: iAnimationBytes (validated >= sizeof(mHeader) above) is the region's true size. Each section
	// is aliased at the current cursor, then BoundAdvance steps both the cursor and the running offset by the section
	// size after checking it fits — so a corrupt-but-in-range count cannot walk these reads off the eager pack buffer.
	int64_t iOffset = static_cast<int64_t>(sizeof(mHeader));
	auto BoundAdvance = [&](int64_t iSectionBytes)
	{
		if (iOffset > iAnimationBytes - iSectionBytes) // overflow-safe form of iOffset + iSectionBytes > iAnimationBytes
		{
			throw common::CorruptStreamException("AnimationData::Load");
		}
		iOffset += iSectionBytes;
		pAnimationData += iSectionBytes;
	};

	// Point into pack memory - no copying
	mpNodes = reinterpret_cast<const common::ModelNode*>(pAnimationData);
	BoundAdvance(mHeader.skeleton.uiNodeCount * static_cast<int64_t>(sizeof(common::ModelNode)));

	mpSkinJointToNode = reinterpret_cast<const uint16_t*>(pAnimationData);
	BoundAdvance(common::RoundUp<int64_t, common::kiAnimationSectionAlignment>(mHeader.skeleton.uiSkinJointCount * static_cast<int64_t>(sizeof(uint16_t))));

	// Inverse bind matrices: read via pointer, pre-compute into aligned array
	const XMFLOAT4X4* pInverseBindMatrices = reinterpret_cast<const XMFLOAT4X4*>(pAnimationData);
	BoundAdvance(mHeader.skeleton.uiSkinJointCount * static_cast<int64_t>(sizeof(XMFLOAT4X4)));

	mpAnimations = reinterpret_cast<const common::AnimationClip*>(pAnimationData);
	BoundAdvance(mHeader.uiAnimationCount * static_cast<int64_t>(sizeof(common::AnimationClip)));

	mpMaterialInfos = reinterpret_cast<const common::MaterialInfo*>(pAnimationData);
	BoundAdvance(mHeader.uiMaterialCount * static_cast<int64_t>(sizeof(common::MaterialInfo)));

	mpChannels = reinterpret_cast<const common::AnimationChannel*>(pAnimationData);
	BoundAdvance(mHeader.uiChannelCount * static_cast<int64_t>(sizeof(common::AnimationChannel)));

	mpKeyframes = reinterpret_cast<const common::AnimationKeyframe*>(pAnimationData);
	BoundAdvance(mHeader.uiKeyframeCount * static_cast<int64_t>(sizeof(common::AnimationKeyframe)));

	mpCubicKeyframes = reinterpret_cast<const common::AnimationKeyframeCubic*>(pAnimationData);
	BoundAdvance(mHeader.uiCubicKeyframeCount * static_cast<int64_t>(sizeof(common::AnimationKeyframeCubic)));

	// Trust boundary (secondary indices): indices stored inside the now count-bounded records still index the
	// fixed-size node arrays / alias pointers. A corrupt chunk with in-range counts can point these out of bounds
	// — e.g. a channel's uiNodeIndex OOB-*writes* mbAnimatedNodes below, and a skin joint->node entry OOB-reads
	// pWorldMatrices in EvaluateMaterial. Validate every on-disk index once here so the per-frame evaluate path
	// stays unchecked, including the node parent indices that enable the single-pass world-matrix build.
	for (uint32_t i = 0; i < mHeader.skeleton.uiSkinJointCount; ++i)
	{
		if (mpSkinJointToNode[i] >= mHeader.skeleton.uiNodeCount)
		{
			throw common::CorruptStreamException("AnimationData::Load");
		}
	}
	for (uint32_t i = 0; i < mHeader.uiMaterialCount; ++i)
	{
		if (static_cast<int32_t>(mpMaterialInfos[i].iParentNodeIndex) >= static_cast<int32_t>(mHeader.skeleton.uiNodeCount))
		{
			throw common::CorruptStreamException("AnimationData::Load");
		}
	}
	for (uint32_t i = 0; i < mHeader.uiChannelCount; ++i)
	{
		const common::AnimationChannel& rChannel = mpChannels[i];
		uint32_t uiKeyframeTotal = rChannel.uiInterpolation == common::AnimationChannel::kInterpolationCubicSpline ? mHeader.uiCubicKeyframeCount : mHeader.uiKeyframeCount;
		if (rChannel.uiNodeIndex >= mHeader.skeleton.uiNodeCount
			|| rChannel.uiKeyframeCount == 0
			|| rChannel.uiKeyframeStart > uiKeyframeTotal
			|| rChannel.uiKeyframeCount > uiKeyframeTotal - rChannel.uiKeyframeStart)
		{
			throw common::CorruptStreamException("AnimationData::Load");
		}
	}
	for (uint32_t i = 0; i < mHeader.uiAnimationCount; ++i)
	{
		const common::AnimationClip& rClip = mpAnimations[i];
		if (rClip.uiChannelStart > mHeader.uiChannelCount
			|| rClip.uiChannelCount > mHeader.uiChannelCount - rClip.uiChannelStart)
		{
			throw common::CorruptStreamException("AnimationData::Load");
		}
	}

	// Verify the exact root sentinel and topological order required by the single-pass world-matrix build.
	for (uint32_t i = 0; i < mHeader.skeleton.uiNodeCount; ++i)
	{
		int64_t iParentIndex = mpNodes[i].iParentIndex;
		if (iParentIndex != -1 && (iParentIndex < 0 || iParentIndex >= static_cast<int64_t>(i)))
		{
			throw common::CorruptStreamException("AnimationData::Load");
		}
	}

	// Pre-compute bind-pose local matrices
	mpBindPoseLocalMatrices = common::MakeAligned<XMMATRIX>(mHeader.skeleton.uiNodeCount);
	for (uint32_t i = 0; i < mHeader.skeleton.uiNodeCount; ++i)
	{
		const common::ModelNode& rNode = mpNodes[i];
		XMMATRIX matBind = XMLoadFloat4x4(&rNode.f4x4BindMatrix);
		XMMATRIX matS = XMMatrixScalingFromVector(XMLoadFloat4(&rNode.f4BindScale));
		XMMATRIX matR = XMMatrixRotationQuaternion(XMLoadFloat4(&rNode.f4BindRotation));
		XMMATRIX matT = XMMatrixTranslationFromVector(XMLoadFloat4(&rNode.f4BindTranslation));
		mpBindPoseLocalMatrices[i] = matBind * matS * matR * matT;
	}

	// Build per-animation animated node masks (1D, row stride = uiNodeCount; uiNodeIndex bounded above)
	mbAnimatedNodes.assign(static_cast<size_t>(mHeader.uiAnimationCount) * mHeader.skeleton.uiNodeCount, 0);
	for (uint32_t iAnim = 0; iAnim < mHeader.uiAnimationCount; ++iAnim)
	{
		const common::AnimationClip& rClip = mpAnimations[iAnim];
		for (uint32_t iCh = rClip.uiChannelStart; iCh < rClip.uiChannelStart + rClip.uiChannelCount; ++iCh)
		{
			mbAnimatedNodes[iAnim * mHeader.skeleton.uiNodeCount + mpChannels[iCh].uiNodeIndex] = 1;
		}
	}

	// Pre-load aligned inverse bind matrices
	mpAlignedInverseBindMatrices = common::MakeAligned<XMMATRIX>(mHeader.skeleton.uiSkinJointCount);
	for (uint32_t i = 0; i < mHeader.skeleton.uiSkinJointCount; ++i)
	{
		mpAlignedInverseBindMatrices[i] = XMLoadFloat4x4(&pInverseBindMatrices[i]);
	}

	// Pre-load aligned relative transforms
	mpAlignedRelativeTransforms = common::MakeAligned<XMMATRIX>(mHeader.uiMaterialCount);
	for (uint32_t i = 0; i < mHeader.uiMaterialCount; ++i)
	{
		mpAlignedRelativeTransforms[i] = XMLoadFloat4x4(&mpMaterialInfos[i].f4x4RelativeTransform);
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
	auto pWorldMatrices = common::gpThreadLocal->mWorkbuffer.PushBuffer<XMMATRIX*>(kiMaxNodes * static_cast<int64_t>(sizeof(XMMATRIX)));
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
	if (rChannel.uiInterpolation == common::AnimationChannel::kInterpolationCubicSpline)
	{
		const common::AnimationKeyframeCubic* pKeyframes = &mpCubicKeyframes[rChannel.uiKeyframeStart];

		uint32_t uiKeyframe0 = 0;
		uint32_t uiKeyframe1 = 0;

		FindKeyframePair(pKeyframes, uiKeyframeCount, fTime, uiKeyframe0, uiKeyframe1);

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
		if (rChannel.uiTargetPath == common::AnimationChannel::kTargetPathRotation)
		{
			vecResult = XMQuaternionNormalize(vecResult);
		}

		return vecResult;
	}

	// STEP/LINEAR path: uses compact AnimationKeyframe without tangent fields
	const common::AnimationKeyframe* pKeyframes = &mpKeyframes[rChannel.uiKeyframeStart];

	uint32_t uiKeyframe0 = 0;
	uint32_t uiKeyframe1 = 0;

	FindKeyframePair(pKeyframes, uiKeyframeCount, fTime, uiKeyframe0, uiKeyframe1);

	const common::AnimationKeyframe& rKey0 = pKeyframes[uiKeyframe0];
	const common::AnimationKeyframe& rKey1 = pKeyframes[uiKeyframe1];

	// STEP interpolation
	if (rChannel.uiInterpolation == common::AnimationChannel::kInterpolationStep || uiKeyframe0 == uiKeyframe1)
	{
		return XMLoadFloat4(&rKey0.f4Value);
	}

	// LINEAR interpolation
	float fDelta = rKey1.fTime - rKey0.fTime;
	float fT = (fTime - rKey0.fTime) / fDelta;

	XMVECTOR vec0 = XMLoadFloat4(&rKey0.f4Value);
	XMVECTOR vec1 = XMLoadFloat4(&rKey1.f4Value);

	// Use slerp for rotations (quaternions)
	if (rChannel.uiTargetPath == common::AnimationChannel::kTargetPathRotation)
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
	const common::AnimationClip& rAnimation = mpAnimations[iAnimationIndex];

	// Allocate temporary TRS arrays from the thread-local workbuffer
	constexpr int64_t kiMaxNodes = common::Skeleton::kiMaxNodes;
	constexpr int64_t kiVecSize = kiMaxNodes * static_cast<int64_t>(sizeof(XMVECTOR));
	constexpr int64_t kiTotalSize = 3 * kiVecSize;

	auto pBufferAlloc = common::gpThreadLocal->mWorkbuffer.PushBuffer<std::byte*>(kiTotalSize);
	std::byte* pBuffer = static_cast<std::byte*>(pBufferAlloc);
	XMVECTOR* pTranslations = reinterpret_cast<XMVECTOR*>(pBuffer);
	XMVECTOR* pRotations    = reinterpret_cast<XMVECTOR*>(pBuffer + kiVecSize);
	XMVECTOR* pScales       = reinterpret_cast<XMVECTOR*>(pBuffer + 2 * kiVecSize);

	// Initialize node transforms from bind pose (only for animated nodes)
	const uint8_t* pbAnimated = mbAnimatedNodes.data() + iAnimationIndex * mHeader.skeleton.uiNodeCount;
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
			case common::AnimationChannel::kTargetPathTranslation:
				pTranslations[rChannel.uiNodeIndex] = vecValue;
				break;
			case common::AnimationChannel::kTargetPathRotation:
				pRotations[rChannel.uiNodeIndex] = vecValue;
				break;
			case common::AnimationChannel::kTargetPathScale:
				pScales[rChannel.uiNodeIndex] = vecValue;
				break;
			default:
				ASSERT(false);
				break;
		}
	}

	// Build local matrices and compute world matrices in a single pass
	// Topological ordering (parent index < child index) guarantees parent world matrix is ready
	for (int64_t i = 0; i < mHeader.skeleton.uiNodeCount; ++i)
	{
		XMMATRIX matLocal {};
		if (pbAnimated[i])
		{
			const common::ModelNode& rNode = mpNodes[i];
			XMMATRIX matBindMatrix = XMLoadFloat4x4(&rNode.f4x4BindMatrix);
			XMMATRIX matScale = XMMatrixScalingFromVector(pScales[i]);
			XMMATRIX matRotation = XMMatrixRotationQuaternion(pRotations[i]);
			XMMATRIX matTranslation = XMMatrixTranslationFromVector(pTranslations[i]);
			// Combine: matrix * S * R * T (row-major; equivalent to T * R * S * matrix in column-major)
			matLocal = matBindMatrix * matScale * matRotation * matTranslation;
		}
		else
		{
			matLocal = mpBindPoseLocalMatrices[i];
		}

		int16_t iParent = mpNodes[i].iParentIndex;
		pWorldMatrices[i] = iParent >= 0 ? matLocal * pWorldMatrices[iParent] : matLocal;
	}
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
		XMMATRIX matRelative = mpAlignedRelativeTransforms[iMaterialIndex];
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
		// Compute inverse of mesh world matrix
		XMMATRIX matMeshWorldInverse = XMMatrixInverse(nullptr, matMeshWorld);

		// Compute joint matrices: inverseBind * nodeWorld * inv(meshWorld)
		// Write to separate joint matrix buffer at the specified offset
		// NO explicit transpose - storage conversion handles row-major to column-major
		for (int64_t i = 0; i < mHeader.skeleton.uiSkinJointCount && i < common::kiMaxJointsPerMesh; ++i)
		{
			uint16_t uiNodeIndex = mpSkinJointToNode[i];
			XMMATRIX matInverseBind = mpAlignedInverseBindMatrices[i];
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

void LoadAnimationDataFromEagerChunks()
{
	// Device loss recreates Graphics in place — the map and the pack memory it points into outlive Graphics
	if (!gAnimationDataMap.empty())
	{
		return;
	}

	for (const auto& [rCrc, rChunk] : gpFileManager->GetEagerChunkMap())
	{
		if (rChunk.pHeader->flags & common::ChunkFlags::kScene && rChunk.pHeader->sceneHeader.bHasAnimation)
		{
			// Animation data comes after scene arrays and material data (aligned to 16 bytes, matching export)
			const int64_t iAnimationSectionOffset = common::SceneHeader::AnimationSectionOffset(rChunk.pHeader->sceneHeader.uiTextureCount, rChunk.pHeader->sceneHeader.uiMaterialCount);

			AnimationData& rAnimationData = gAnimationDataMap.try_emplace(rCrc).first->second;
			// Trust boundary: eager scene chunks are on-disk pack bytes parsed at boot. A corrupt animation
			// header count throws from Load; boot-required asset, so log kError and let it propagate to
			// MainThread's try/catch (HandleException — crash report + exit), matching the boot hard-fail tier.
			try
			{
				rAnimationData.Load(rChunk.pData + iAnimationSectionOffset, rChunk.iDataSize - iAnimationSectionOffset, rCrc);
			}
			catch (const common::CorruptStreamException& rException)
			{
				char pcHex[20] {};
				LOG(kLoading, kError, "Corrupt animation data for GLTF CRC {}: {}", common::ToHex(std::span(pcHex), rCrc), rException.what());
				throw;
			}
			LOG(kLoading, kDebug, "Loaded animation data for GLTF CRC {}: {} nodes, {} skin joints, {} animations", rCrc, rAnimationData.mHeader.skeleton.uiNodeCount, rAnimationData.mHeader.skeleton.uiSkinJointCount, rAnimationData.mHeader.uiAnimationCount);
		}
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
