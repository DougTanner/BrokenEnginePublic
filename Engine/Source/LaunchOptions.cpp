#include "Pch.h"

#include "LaunchOptions.h"

#pragma comment(lib, "Shell32.lib") // CommandLineToArgvW

namespace engine
{

bool ParseLaunchOptions()
{
	// Runs in wWinMain before allocation tracking and before any ThreadLocal exists — LOG uses the per-thread
	// fallback buffer here and any allocation is untracked. CommandLineToArgvW allocates via LocalAlloc; LocalFree it.
	int iArgumentCount = 0;
	LPWSTR* pArgumentValues = CommandLineToArgvW(GetCommandLineW(), &iArgumentCount);
	if (pArgumentValues == nullptr)
	{
		LOG(kDefault, kWarning, "ParseLaunchOptions CommandLineToArgvW failed: {}", GetLastError());
		return true;
	}

	bool bOk = true;
	for (int i = 1; i < iArgumentCount; ++i)
	{
		if (wcscmp(pArgumentValues[i], L"--loopback-only") == 0)
		{
			gLaunchOptions.flags.Set(LaunchOptionFlags::kLoopbackOnly);
		}
		else if (wcscmp(pArgumentValues[i], L"--renderdoc") == 0)
		{
			gLaunchOptions.flags.Set(LaunchOptionFlags::kRenderDoc);
		}
		else if (wcscmp(pArgumentValues[i], L"--agent-port") == 0)
		{
			if (i + 1 < iArgumentCount)
			{
				int64_t iPort = static_cast<int64_t>(std::wcstoll(pArgumentValues[++i], nullptr, 10));
				// Reject out-of-range ports: htons truncates to uint16_t, so the listener would bind a port the
				// agent was never told about (65636 -> 100, -1 -> 65535). Fail fast — do not run with a mangled port.
				if (iPort < 1 || iPort > 65535)
				{
					LOG(kDefault, kError, "Launch option --agent-port out of range [1, 65535]: {}", iPort);
					gLaunchOptions.iAgentPort = 0;
					bOk = false;
				}
				else
				{
					gLaunchOptions.iAgentPort = iPort;
				}
			}
			else
			{
				LOG(kDefault, kWarning, "Launch option --agent-port missing value");
			}
		}
		else if (wcscmp(pArgumentValues[i], L"--log-file") == 0)
		{
			if (i + 1 < iArgumentCount)
			{
				gLaunchOptions.logFile = pArgumentValues[++i];
			}
			else
			{
				LOG(kDefault, kWarning, "Launch option --log-file missing value");
			}
		}
		else if (wcscmp(pArgumentValues[i], L"--data-directory") == 0)
		{
			gLaunchOptions.dataDirectory.clear();
			if (i + 1 >= iArgumentCount)
			{
				LOG(kDefault, kError, "Launch option --data-directory missing value");
				bOk = false;
				continue;
			}

			std::filesystem::path requestedDirectory = pArgumentValues[++i];
			if (!requestedDirectory.is_absolute())
			{
				LOG(kDefault, kError, "Launch option --data-directory must be absolute: \"{}\"", requestedDirectory);
				bOk = false;
				continue;
			}

			std::error_code canonicalError;
			std::filesystem::path canonicalDirectory = std::filesystem::canonical(requestedDirectory, canonicalError);
			if (canonicalError)
			{
				LOG(kDefault, kError, "Launch option --data-directory is missing or unusable: \"{}\" (error {})", requestedDirectory, canonicalError.value());
				bOk = false;
				continue;
			}

			std::error_code typeError;
			bool bIsDirectory = std::filesystem::is_directory(canonicalDirectory, typeError);
			if (typeError || !bIsDirectory)
			{
				LOG(kDefault, kError, "Launch option --data-directory is not a usable directory: \"{}\" (error {})", canonicalDirectory, typeError.value());
				bOk = false;
				continue;
			}

			gLaunchOptions.dataDirectory = std::move(canonicalDirectory);
		}
		else if (wcscmp(pArgumentValues[i], L"--windowed") == 0)
		{
			if (i + 1 < iArgumentCount)
			{
				// Parse "WxH" (case-insensitive separator). Reject malformed or out-of-range dimensions — a zero
				// extent would size a degenerate window / swapchain, and an oversized value wraps negative at Main's
				// LONG window cast or truncates at the uint32 extent cast below. Bound to [1, kiMaxDimension].
				constexpr int64_t kiMaxDimension = 16384;
				const wchar_t* pcValue = pArgumentValues[++i];
				wchar_t* pcEnd = nullptr;
				int64_t iWidth = static_cast<int64_t>(std::wcstoll(pcValue, &pcEnd, 10));
				int64_t iHeight = 0;
				if (pcEnd != nullptr && pcEnd != pcValue && (*pcEnd == L'x' || *pcEnd == L'X'))
				{
					iHeight = static_cast<int64_t>(std::wcstoll(pcEnd + 1, &pcEnd, 10));
				}
				if (iWidth < 1 || iWidth > kiMaxDimension || iHeight < 1 || iHeight > kiMaxDimension || pcEnd == nullptr || *pcEnd != L'\0')
				{
					LOG(kDefault, kError, "Launch option --windowed expects WxH with dimensions in [1, 16384]: {}", std::wstring(pcValue));
					gLaunchOptions.windowedExtent = VkExtent2D {0, 0};
					bOk = false;
				}
				else
				{
					gLaunchOptions.windowedExtent = VkExtent2D {static_cast<uint32_t>(iWidth), static_cast<uint32_t>(iHeight)};
				}
			}
			else
			{
				LOG(kDefault, kWarning, "Launch option --windowed missing value");
			}
		}
		else
		{
			LOG(kDefault, kWarning, "Unknown launch option: {}", std::wstring(pArgumentValues[i]));
		}
	}

	LocalFree(pArgumentValues);
	return bOk;
}

} // namespace engine
