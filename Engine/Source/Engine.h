#pragma once

// Profile
#include "Profile/ProfileManagerBase.h"

// Frame
#include "Frame/AreaDamage.h"
#include "Frame/Collision.h"
#include "Frame/TimeStep.h"

// File (FileManager before DifferenceStream: DifferenceStream uses FileFlags_t)
#include "File/FileManager.h"
#include "File/DifferenceStream.h"

// Network (shared)
#include "Network/NetworkManager.h"
#include "Network/NetworkProtocol.h"
#include "Network/NetworkSerialization.h"
#include "Network/NetworkSimulation.h"

#if defined(BT_CLIENT)

// Ui (client-only)
#include "Ui/Screens/TweaksScreen/TweaksScreenBase.h"
#include "Ui/Screens/TweaksScreen/TweaksSliderMap.h"
#include "Ui/NetworkUiControl.h"

// Debug (Vulkan enum stringification, client-only)
#include "Debug/EnumToString.h"

// Graphics objects
#include "Graphics/Objects/Buffer.h"
#include "Graphics/Objects/CommandBuffers.h"
#include "Graphics/Objects/Pipeline.h"
#include "Graphics/Objects/ModelPipeline.h"
#include "Graphics/Objects/Shader.h"
#include "Graphics/Objects/Texture.h"

// Graphics managers
#include "Graphics/Graphics.h"
#include "Graphics/GraphicsUtils.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/CommandBufferManager.h"
#include "Graphics/Managers/DeviceManager.h"
#include "Graphics/Managers/ImGuiManager.h"
#include "Graphics/Managers/InstanceManager.h"
#include "Graphics/Managers/ParticleManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Graphics/Managers/SwapchainManager.h"
#include "Graphics/Managers/TextManager.h"
#include "Graphics/Managers/TextureManager.h"
#include "Graphics/Managers/TextureUploadManager.h"

// Graphics misc
#include "Graphics/AnimationData.h"
#include "Graphics/CameraBase.h"
#include "Graphics/OneShotCommandBuffer.h"
#include "Graphics/Screenshot.h"

// Render
#include "Graphics/Render/Render.h"

// Graphics debug
#include "Graphics/Debug/DebugRender.h"

// Audio (voices before managers: AudioManager uses StaticVoices and StreamingVoices)
#include "Audio/StaticVoice.h"
#include "Audio/StreamingVoice.h"
#include "Audio/StaticVoices.h"
#include "Audio/StreamingVoices.h"
#include "Audio/AudioManager.h"

// Input
#include "Input/RawInputManager.h"

// Network client
#include "Network/NetworkDiscoveryScanner.h"
#include "Network/Client/Client.h"
#include "Network/Client/ClientSessionBase.h"

// Graphics (Islands is client-only GPU rendering)
#include "Graphics/Islands.h"

#endif // BT_CLIENT

// Terrain collision data (both client and server)
#include "Frame/IslandTerrain.h"

#if defined(BT_SERVER)
#include "Network/NetworkDiscoveryResponder.h"
#include "Network/Server/Server.h"
#include "Network/Server/ServerSessionBase.h"
#include "Server/ServerDisplay.h"
#endif

// Engine base
#include "GameBase.h"

// Formatters for engine types used by LogDifference
template<>
struct std::formatter<engine::alignment_t> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const engine::alignment_t alignment, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{}", alignment.Value());
	}
};

template<>
struct std::formatter<engine::Alignments> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const engine::Alignments& rAlignments, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "Alignments({})", rAlignments.alignmentPairs.size());
	}
};

template<>
struct std::formatter<engine::uuid_t> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const engine::uuid_t id, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{}", id.Value());
	}
};

template<typename T>
struct std::formatter<engine::id_t<T>> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const engine::id_t<T> id, CONTEXT& rContext) const
	{
		return std::format_to(rContext.out(), "{}", id.ToUuid().Value());
	}
};
