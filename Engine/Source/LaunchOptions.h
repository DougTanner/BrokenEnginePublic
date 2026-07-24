#pragma once

namespace engine
{

enum class LaunchOptionFlags : uint8_t
{
	kLoopbackOnly = 1 << 0, // bind game/discovery sockets to loopback and disable LAN discovery broadcast
	kRenderDoc    = 1 << 1, // force-load renderdoc.dll before instance creation and expose the in-app capture API (client; requires game kbRenderDoc)
};

// Command-line launch options, parsed once in wWinMain before any subsystem starts. Later harness plans append
// fields here as new launch args are added. std::filesystem::path / int64_t come from ExternalHeaders (PCH).
struct LaunchOptions
{
	int64_t iAgentPort = 0; // 0 = agent command channel disabled
	common::Flags<LaunchOptionFlags> flags; // parsed boolean launch options (see LaunchOptionFlags)
	std::filesystem::path logFile; // empty = no log-file sink
	std::filesystem::path dataDirectory; // empty = executable-sibling Data; --data-directory supplies an existing absolute directory
	VkExtent2D windowedExtent {0, 0}; // {0,0} = not requested; --windowed WxH forces a windowed client size (overrides fullscreen at the read sites, never mutates gFullscreen)
};

inline LaunchOptions gLaunchOptions {};

// Parse GetCommandLineW() into gLaunchOptions. Unknown args log kWarning and are ignored. Returns false on a
// fatal argument error (e.g. --agent-port out of [1, 65535]) — the caller must abort startup.
bool ParseLaunchOptions();

// True for an agent-harness client: a kbAgent build launched with --agent-port. Every physical (human) input
// chokepoint is suppressed for the process lifetime, leaving the harness's synthetic input as the sole source.
// Constant from the wWinMain parse (equals agent-channel existence) — never flips mid-run. Reading game kbAgent is the
// sanctioned engine->game direction. Harness overview: .claude/skills/agent-harness/SKILL.md.
inline bool PhysicalInputSuppressed()
{
	return kbAgent && gLaunchOptions.iAgentPort != 0;
}

#if defined(BT_CLIENT)
// Agent-only runtime fullscreen override. std::nullopt clears it (restores launch behavior); a set value forces
// windowed/fullscreen mid-run, consulted ahead of --windowed in WantedFullscreen(). Never mutates the persisted
// gFullscreen setting. Defined in Main.cpp beside the override static. Harness overview: .claude/skills/agent-harness/SKILL.md.
void SetAgentFullscreenOverride(std::optional<bool> fullscreen);
#endif

} // namespace engine
