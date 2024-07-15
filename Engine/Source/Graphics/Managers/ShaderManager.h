#pragma once

#include "Graphics/Objects/Shader.h"

namespace engine
{

class DeviceManager;

class ShaderManager
{
public:

	ShaderManager();
	~ShaderManager();

	std::unordered_map<common::crc_t, Shader> mShaders;
};

inline ShaderManager* gpShaderManager = nullptr;

} // namespace engine
