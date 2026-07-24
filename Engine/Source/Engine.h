#pragma once

// Profile
#include "Profile/ProfileManagerBase.h"

// Frame
#include "Frame/AreaDamage.h"
#include "Frame/Collision.h"
#include "Frame/TimeStep.h"

// File
#include "File/FileManager.h"
#include "File/DifferenceStream.h"

// Network (shared)
#include "Network/NetworkManager.h"
#include "Network/NetworkProtocol.h"
#include "Network/NetworkMessages.h"
#include "Network/NetworkSessionContract.h"
#include "Network/NetworkSerialization.h"
#include "Network/NetworkSimulation.h"

// Launch options and agent command channel (shared)
#include "LaunchOptions.h"
#include "Agent/AgentCommandServer.h"
#include "Agent/AgentCommandsShared.h"

#if defined(BT_CLIENT)

// Ui (client-only)
#include "Ui/Screens/TweaksScreen/TweaksScreenBase.h"
#include "Ui/Screens/TweaksScreen/TweaksSliderMap.h"
#include "Ui/NetworkUiControl.h"

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

// Audio (StaticVoice/StreamingVoice headers deliberately not aggregated)
#include "Audio/AudioUtility.h"
#include "Audio/AudioManager.h"

// Input
#include "Input/RawInputManager.h"

// Agent client-only synthetic input + UI widget registry (self-guarded; grouped here in the BT_CLIENT span)
#include "Agent/AgentUiRegistry.h"
#include "Agent/AgentInput.h"

// Network client
#include "Network/NetworkDiscoveryScanner.h"
#include "Network/Client/Client.h"
#include "Network/Client/ClientSessionRuntime.h"

// Graphics (Islands is client-only GPU rendering)
#include "Graphics/Islands.h"

#endif // BT_CLIENT

// Terrain collision data (both client and server)
#include "Frame/IslandTerrain.h"

#if defined(BT_SERVER)
#include "Network/NetworkDiscoveryResponder.h"
#include "Network/Server/Server.h"
#include "Network/Server/ServerSessionRuntime.h"
#endif

// Engine base
#include "GameBase.h"

// Formatters for engine types used by LogDifference.
// global_id_t's formatter stays with its type in Frame/Collections/CollectionId.h.
template<>
struct std::formatter<engine::alignment_t> : std::formatter<uint32_t>
{
	template<typename CONTEXT>
	auto format(const engine::alignment_t alignment, CONTEXT& rContext) const
	{
		return std::formatter<uint32_t>::format(alignment.Value(), rContext);
	}
};

template<>
struct std::formatter<engine::Alignments> : std::formatter<std::string_view>
{
	template<typename CONTEXT>
	auto format(const engine::Alignments& rAlignments, CONTEXT& rContext) const
	{
		char pcBuffer[48];
		char* pWrite = pcBuffer;
		for (char c : std::string_view("Alignments("))
		{
			*(pWrite++) = c;
		}
		pWrite = std::to_chars(pWrite, pcBuffer + sizeof(pcBuffer), rAlignments.alignmentPairs.size()).ptr;
		*(pWrite++) = ')';
		return std::formatter<std::string_view>::format(std::string_view(pcBuffer, pWrite - pcBuffer), rContext);
	}
};

template<>
struct std::formatter<engine::uuid_t> : std::formatter<int64_t>
{
	template<typename CONTEXT>
	auto format(const engine::uuid_t id, CONTEXT& rContext) const
	{
		return std::formatter<int64_t>::format(id.Value(), rContext);
	}
};

template<typename T>
struct std::formatter<engine::id_t<T>> : std::formatter<int64_t>
{
	template<typename CONTEXT>
	auto format(const engine::id_t<T> id, CONTEXT& rContext) const
	{
		return std::formatter<int64_t>::format(id.ToUuid().Value(), rContext);
	}
};
