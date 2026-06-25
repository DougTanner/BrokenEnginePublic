#include "SceneAnimationLoader.h"

namespace
{

// Returns a pointer to an accessor's tightly-packed float data (keyframe times / values).
const float* AccessorFloats(const tinygltf::Model& rModel, int iAccessor)
{
	const tinygltf::Accessor& rAccessor = rModel.accessors[iAccessor];
	ASSERT(rAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
	const tinygltf::BufferView& rBufferView = rModel.bufferViews[rAccessor.bufferView];
	return reinterpret_cast<const float*>(&(rModel.buffers[rBufferView.buffer].data[rAccessor.byteOffset + rBufferView.byteOffset]));
}

}

bool DetermineAnimationPath(const tinygltf::Model& rGltfModel)
{
	if (rGltfModel.skins.empty())
	{
		return false;
	}

	const tinygltf::Skin& rSkin = rGltfModel.skins[0];
	std::unordered_set<int> skinJoints(rSkin.joints.begin(), rSkin.joints.end());

	for (const tinygltf::Animation& rAnim : rGltfModel.animations)
	{
		for (const tinygltf::AnimationChannel& rChannel : rAnim.channels)
		{
			if (rChannel.target_node >= 0 && skinJoints.count(rChannel.target_node) == 0)
			{
				return false;
			}
		}
	}
	return true;
}

void LoadAnimations(const tinygltf::Model& rModel, AnimationOutput& rOut)
{
	std::vector<common::AnimationClip>& rAnimations = rOut.rAnimations;
	std::vector<common::AnimationChannel>& rChannels = rOut.rChannels;
	std::vector<common::AnimationKeyframe>& rKeyframes = rOut.rKeyframes;
	std::vector<common::AnimationKeyframeCubic>& rCubicKeyframes = rOut.rCubicKeyframes;

	LOG(kDefault, kDebug, "LoadAnimations: {} nodes", rModel.nodes.size());

	for (const tinygltf::Animation& rAnim : rModel.animations)
	{
		common::AnimationClip animation {};

		// Set name
		size_t iNameLength = std::min(rAnim.name.size(), static_cast<size_t>(common::AnimationClip::kiMaxNameLength - 1));
		std::memcpy(animation.pcName, rAnim.name.c_str(), iNameLength);
		animation.pcName[iNameLength] = '\0';

		animation.uiChannelStart = static_cast<uint32_t>(rChannels.size());
		animation.uiChannelCount = 0;
		animation.fDuration = 0.0f;

		for (const tinygltf::AnimationChannel& rGltfChannel : rAnim.channels)
		{
			// Skip channels targeting an invalid / out-of-range node
			if (rGltfChannel.target_node < 0 || rGltfChannel.target_node >= static_cast<int>(rModel.nodes.size()))
			{
				LOG(kDefault, kVerbose, "  FILTERED: channel targeting node {} out of range (node count {})", rGltfChannel.target_node, rModel.nodes.size());
				continue;
			}
			LOG(kDefault, kVerbose, "  KEPT: channel targeting node {} (\"{}\") -> node index {}", rGltfChannel.target_node, rModel.nodes.at(rGltfChannel.target_node).name, rGltfChannel.target_node);

			const tinygltf::AnimationSampler& rSampler = rAnim.samplers[rGltfChannel.sampler];

			common::AnimationChannel channel {};
			channel.uiNodeIndex = static_cast<uint16_t>(rGltfChannel.target_node);

			// Target path
			if (rGltfChannel.target_path == "translation")
			{
				channel.uiTargetPath = common::AnimationChannel::kTargetPathTranslation;
			}
			else if (rGltfChannel.target_path == "rotation")
			{
				channel.uiTargetPath = common::AnimationChannel::kTargetPathRotation;
			}
			else if (rGltfChannel.target_path == "scale")
			{
				channel.uiTargetPath = common::AnimationChannel::kTargetPathScale;
			}
			else
			{
				continue; // Skip unknown paths
			}

			// Interpolation
			if (rSampler.interpolation == "STEP")
			{
				channel.uiInterpolation = common::AnimationChannel::kInterpolationStep;
			}
			else if (rSampler.interpolation == "CUBICSPLINE")
			{
				channel.uiInterpolation = common::AnimationChannel::kInterpolationCubicSpline;
			}
			else
			{
				channel.uiInterpolation = common::AnimationChannel::kInterpolationLinear;
			}

			// Keyframe times come from the input accessor (which also supplies the keyframe count); values from the output accessor
			const tinygltf::Accessor& rInputAccessor = rModel.accessors[rSampler.input];
			const float* pfTimes = AccessorFloats(rModel, rSampler.input);
			const float* pfValues = AccessorFloats(rModel, rSampler.output);

			channel.uiKeyframeCount = static_cast<uint32_t>(rInputAccessor.count);

			int iValueStride = (channel.uiTargetPath == common::AnimationChannel::kTargetPathRotation) ? 4 : 3; // Rotation is vec4, others vec3

			if (channel.uiInterpolation == common::AnimationChannel::kInterpolationCubicSpline) // CUBICSPLINE
			{
				channel.uiKeyframeStart = static_cast<uint32_t>(rCubicKeyframes.size());

				for (int64_t j = 0; j < static_cast<int64_t>(rInputAccessor.count); ++j)
				{
					common::AnimationKeyframeCubic keyframe {};
					keyframe.fTime = pfTimes[j];

					// CUBICSPLINE has 3 values per keyframe: in-tangent, value, out-tangent
					int64_t iBaseIdx = j * 3 * iValueStride;

					if (channel.uiTargetPath == common::AnimationChannel::kTargetPathRotation) // Rotation (vec4)
					{
						keyframe.f4InTangent = XMFLOAT4(pfValues[iBaseIdx + 0], pfValues[iBaseIdx + 1], pfValues[iBaseIdx + 2], pfValues[iBaseIdx + 3]);
						keyframe.f4Value = XMFLOAT4(pfValues[iBaseIdx + iValueStride + 0], pfValues[iBaseIdx + iValueStride + 1], pfValues[iBaseIdx + iValueStride + 2], pfValues[iBaseIdx + iValueStride + 3]);
						keyframe.f4OutTangent = XMFLOAT4(pfValues[iBaseIdx + 2 * iValueStride + 0], pfValues[iBaseIdx + 2 * iValueStride + 1], pfValues[iBaseIdx + 2 * iValueStride + 2], pfValues[iBaseIdx + 2 * iValueStride + 3]);
					}
					else // Translation/Scale (vec3)
					{
						keyframe.f4InTangent = XMFLOAT4(pfValues[iBaseIdx + 0], pfValues[iBaseIdx + 1], pfValues[iBaseIdx + 2], 0.0f);
						keyframe.f4Value = XMFLOAT4(pfValues[iBaseIdx + iValueStride + 0], pfValues[iBaseIdx + iValueStride + 1], pfValues[iBaseIdx + iValueStride + 2], 0.0f);
						keyframe.f4OutTangent = XMFLOAT4(pfValues[iBaseIdx + 2 * iValueStride + 0], pfValues[iBaseIdx + 2 * iValueStride + 1], pfValues[iBaseIdx + 2 * iValueStride + 2], 0.0f);
					}

					animation.fDuration = std::max(animation.fDuration, keyframe.fTime);
					rCubicKeyframes.push_back(keyframe);
				}
			}
			else // STEP or LINEAR
			{
				channel.uiKeyframeStart = static_cast<uint32_t>(rKeyframes.size());

				for (int64_t j = 0; j < static_cast<int64_t>(rInputAccessor.count); ++j)
				{
					common::AnimationKeyframe keyframe {};
					keyframe.fTime = pfTimes[j];

					if (channel.uiTargetPath == common::AnimationChannel::kTargetPathRotation)
					{
						// Rotation (quaternion)
						keyframe.f4Value = XMFLOAT4(pfValues[j * iValueStride + 0], pfValues[j * iValueStride + 1], pfValues[j * iValueStride + 2], pfValues[j * iValueStride + 3]);
					}
					else if (channel.uiTargetPath == common::AnimationChannel::kTargetPathTranslation)
					{
						// Translation
						keyframe.f4Value = XMFLOAT4(pfValues[j * iValueStride + 0], pfValues[j * iValueStride + 1], pfValues[j * iValueStride + 2], 0.0f);
					}
					else
					{
						// Scale
						keyframe.f4Value = XMFLOAT4(pfValues[j * iValueStride + 0], pfValues[j * iValueStride + 1], pfValues[j * iValueStride + 2], 0.0f);
					}

					animation.fDuration = std::max(animation.fDuration, keyframe.fTime);
					rKeyframes.push_back(keyframe);
				}
			}

			rChannels.push_back(channel);
			++animation.uiChannelCount;
		}

		if (animation.uiChannelCount > 0)
		{
			rAnimations.push_back(animation);
		}
	}
}
