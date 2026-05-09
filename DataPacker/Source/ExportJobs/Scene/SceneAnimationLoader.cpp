#include "SceneAnimationLoader.h"

#include "tinygltf/tiny_gltf.h"

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

void LoadAnimations(const tinygltf::Model& rModel, const std::unordered_map<int, int>& rNodeToNodeIndexMap, std::vector<common::AnimationClip>& rAnimations, std::vector<common::AnimationChannel>& rChannels, std::vector<common::AnimationKeyframe>& rKeyframes, std::vector<common::AnimationKeyframeCubic>& rCubicKeyframes)
{
	LOG(kDefault, kDebug, "LoadAnimations: nodeToNodeIndexMap has {} entries", rNodeToNodeIndexMap.size());

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

		int iFilteredCount = 0;
		int iKeptCount = 0;
		for (const tinygltf::AnimationChannel& rGltfChannel : rAnim.channels)
		{
			// Skip channels for nodes not in the node map
			auto it = rNodeToNodeIndexMap.find(rGltfChannel.target_node);
			if (it == rNodeToNodeIndexMap.end())
			{
				++iFilteredCount;
				LOG(kDefault, kVerbose, "  FILTERED: channel targeting node {} (\"{}\") not in map", rGltfChannel.target_node, rGltfChannel.target_node >= 0 ? rModel.nodes[rGltfChannel.target_node].name : "invalid");
				continue;
			}
			++iKeptCount;
			LOG(kDefault, kVerbose, "  KEPT: channel targeting node {} (\"{}\") -> node index {}", rGltfChannel.target_node, rModel.nodes[rGltfChannel.target_node].name, it->second);

			const tinygltf::AnimationSampler& rSampler = rAnim.samplers[rGltfChannel.sampler];

			common::AnimationChannel channel {};
			channel.uiNodeIndex = static_cast<uint16_t>(it->second);

			// Target path
			if (rGltfChannel.target_path == "translation")
			{
				channel.uiTargetPath = 0;
			}
			else if (rGltfChannel.target_path == "rotation")
			{
				channel.uiTargetPath = 1;
			}
			else if (rGltfChannel.target_path == "scale")
			{
				channel.uiTargetPath = 2;
			}
			else
			{
				continue; // Skip unknown paths
			}

			// Interpolation
			if (rSampler.interpolation == "STEP")
			{
				channel.uiInterpolation = 0;
			}
			else if (rSampler.interpolation == "CUBICSPLINE")
			{
				channel.uiInterpolation = 2;
			}
			else
			{
				channel.uiInterpolation = 1; // LINEAR
			}

			// Load keyframe times from input accessor
			const tinygltf::Accessor& rInputAccessor = rModel.accessors[rSampler.input];
			const tinygltf::BufferView& rInputBufferView = rModel.bufferViews[rInputAccessor.bufferView];
			const float* pfTimes = reinterpret_cast<const float*>(&(rModel.buffers[rInputBufferView.buffer].data[rInputAccessor.byteOffset + rInputBufferView.byteOffset]));

			// Load keyframe values from output accessor
			const tinygltf::Accessor& rOutputAccessor = rModel.accessors[rSampler.output];
			const tinygltf::BufferView& rOutputBufferView = rModel.bufferViews[rOutputAccessor.bufferView];
			const float* pfValues = reinterpret_cast<const float*>(&(rModel.buffers[rOutputBufferView.buffer].data[rOutputAccessor.byteOffset + rOutputBufferView.byteOffset]));

			channel.uiKeyframeCount = static_cast<uint32_t>(rInputAccessor.count);

			int iValueStride = (channel.uiTargetPath == 1) ? 4 : 3; // Rotation is vec4, others vec3

			if (channel.uiInterpolation == 2) // CUBICSPLINE
			{
				channel.uiKeyframeStart = static_cast<uint32_t>(rCubicKeyframes.size());

				for (int64_t j = 0; j < static_cast<int64_t>(rInputAccessor.count); ++j)
				{
					common::AnimationKeyframeCubic keyframe {};
					keyframe.fTime = pfTimes[j];

					// CUBICSPLINE has 3 values per keyframe: in-tangent, value, out-tangent
					int64_t iBaseIdx = j * 3 * iValueStride;

					if (channel.uiTargetPath == 1) // Rotation (vec4)
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

					if (channel.uiTargetPath == 1)
					{
						// Rotation (quaternion)
						keyframe.f4Value = XMFLOAT4(pfValues[j * iValueStride + 0], pfValues[j * iValueStride + 1], pfValues[j * iValueStride + 2], pfValues[j * iValueStride + 3]);
					}
					else if (channel.uiTargetPath == 0)
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
