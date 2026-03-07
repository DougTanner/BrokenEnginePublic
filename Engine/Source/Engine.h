#pragma once

// Debug
#include "Debug/EnumToString.h"

// Profile
#include "Profile/ProfileManagerBase.h"

// Frame
#include "Frame/Collision.h"
#include "Frame/TimeStep.h"

// File (FileManager before DifferenceStream: DifferenceStream uses FileFlags_t)
#include "File/FileManager.h"
#include "File/DifferenceStream.h"

// Network (shared)
#include "Network/NetworkDiscovery.h"
#include "Network/NetworkManager.h"
#include "Network/NetworkProtocol.h"
#include "Network/NetworkSerialization.h"

// Ui
#include "Ui/Screens/TweaksScreen/TweaksScreen.h"

#ifdef BT_CLIENT

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

// Audio (voices before AudioManager: AudioManager uses StaticVoice and StreamingVoice)
#include "Audio/StaticVoice.h"
#include "Audio/StreamingVoice.h"
#include "Audio/AudioManager.h"

// Input
#include "Input/RawInputManager.h"

// Network client
#include "Network/NetworkClient/NetworkClient.h"

// Graphics (Islands is client-only GPU rendering)
#include "Graphics/Islands.h"

#endif // BT_CLIENT

// Terrain collision data (both client and server)
#include "Frame/IslandTerrain.h"

#ifdef BT_SERVER
#include "Network/NetworkServer/NetworkServer.h"
#include "Server/ServerDisplay.h"
#endif

// Engine base
#include "GameBase.h"
