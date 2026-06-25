#pragma once

namespace tinygltf { class Model; }

// Determine whether to use skeletal or node-based animation.
// Returns true for skeletal (all channels target skin joints), false for node-based.
bool DetermineAnimationPath(const tinygltf::Model& rGltfModel);

// Bundle of LoadAnimations output buffers — appended to in place.
struct AnimationOutput
{
	std::vector<common::AnimationClip>& rAnimations;
	std::vector<common::AnimationChannel>& rChannels;
	std::vector<common::AnimationKeyframe>& rKeyframes;
	std::vector<common::AnimationKeyframeCubic>& rCubicKeyframes;
};

void LoadAnimations(const tinygltf::Model& rModel, AnimationOutput& rOut);
