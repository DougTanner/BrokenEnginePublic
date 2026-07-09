#pragma once

namespace engine
{

// Command-line launch options, parsed once in wWinMain before any subsystem starts. Later harness plans append
// fields here as new launch args are added. std::filesystem::path / int64_t come from ExternalHeaders (PCH).
struct LaunchOptions
{
	int64_t iAgentPort = 0; // 0 = agent command channel disabled
	std::filesystem::path logFile; // empty = no log-file sink
	VkExtent2D windowedExtent {0, 0}; // {0,0} = not requested; --windowed WxH forces a windowed client size (overrides fullscreen at the read sites, never mutates gFullscreen)
};

inline LaunchOptions gLaunchOptions {};

// Parse GetCommandLineW() into gLaunchOptions. Unknown args log kWarning and are ignored. Returns false on a
// fatal argument error (e.g. --agent-port out of [1, 65535]) — the caller must abort startup.
bool ParseLaunchOptions();

} // namespace engine
