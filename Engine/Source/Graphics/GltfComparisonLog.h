#pragma once

#include <fstream>
#include <iomanip>
#include <cstdio>

namespace engine
{

inline constexpr int kiComparisonLogVersion = 4;
inline std::ofstream gComparisonLog;
inline bool gbComparisonLoggingEnabled = false;
inline bool gbFirstFrameLogged = false;
inline constexpr common::crc_t kBlackDragonGltfCrc = 12934505000038460124;

inline void InitComparisonLog(common::crc_t crc)
{
	gbComparisonLoggingEnabled = (crc == kBlackDragonGltfCrc);
	if (gbComparisonLoggingEnabled)
	{
		gComparisonLog.open("C:/Users/dougt/Documents/BrokenEnginePublic/gltf_comparison_broken_engine.log");
		gComparisonLog << "=== GLTF Processing Log (BrokenEngine Runtime) === Version " << kiComparisonLogVersion << std::endl;
		gComparisonLog << std::fixed << std::setprecision(6);
	}
}

inline void CloseComparisonLog()
{
	if (gbComparisonLoggingEnabled)
	{
		gComparisonLog << "\n=== END GLTF Processing Log ===" << std::endl;
		gComparisonLog.close();
		gbComparisonLoggingEnabled = false;
	}
}

template<typename... ARGS>
void CompLog(const char* pcFormat, ARGS... args)
{
	// Check if log is open instead of gbComparisonLoggingEnabled (inline variable linkage issues)
	if (!gComparisonLog.is_open())
	{
		return;
	}
	char pcBuffer[4096];
	snprintf(pcBuffer, sizeof(pcBuffer), pcFormat, args...);
	gComparisonLog << pcBuffer << std::endl;
}

} // namespace engine
