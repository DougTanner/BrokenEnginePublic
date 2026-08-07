#include "Memory/GlobalAllocator.h"

#include "CrashReport.h"
#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Profile/ProfileManager.h"
#include "Server/ServerDisplay.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/Screens/TweaksScreen/TweaksScreen.h"

namespace engine
{

static bool sbQuit = false;

void RequestQuit()
{
	sbQuit = true;
}

#if defined(BT_CLIENT)
static HCURSOR sHcursorArrow = nullptr;
static HCURSOR sHcursorCrosshair = nullptr;
#endif

static HWND sHwnd = nullptr;
static HMONITOR sHmonitor = nullptr;
static MONITORINFO sMonitorInfo {};

static bool sbHasFocus = false;

#if defined(BT_CLIENT)
static LONG sWindowStyle = 0;
static RECT sWindowRect {};

// Agent-only runtime fullscreen override (SetAgentFullscreenOverride). std::nullopt = no override (launch behavior);
// set = the agent fullscreen command forces this mode, consulted ahead of --windowed. Never mutates gFullscreen.
static std::optional<bool> sAgentFullscreenOverride;
#endif

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

#if defined(BT_CLIENT)
void FindMonitor(bool bUseCurrentRect);
VkExtent2D SetupWindow(bool bFullscreen, LONG& riWindowStyle, RECT& rWindowRect);

// Effective fullscreen precedence: the agent fullscreen override (runtime, in-memory) wins first; else false when
// --windowed WxH is set (reproducible agent capture geometry); else the saved gFullscreen preference. Override only
// at the read sites — never gFullscreen.Set() (it is persisted to GraphicsSettings.bin, so mutating it would silently
// rewrite the user's saved fullscreen preference).
static bool WantedFullscreen()
{
	if (sAgentFullscreenOverride.has_value())
	{
		return *sAgentFullscreenOverride;
	}
	if (gLaunchOptions.windowedExtent.width != 0 && gLaunchOptions.windowedExtent.height != 0)
	{
		return false;
	}
	return gFullscreen.Get<bool>();
}

void SetAgentFullscreenOverride(std::optional<bool> fullscreen)
{
	sAgentFullscreenOverride = fullscreen;
}
#endif
bool ProcessMessages();

void MainThread(HINSTANCE hinstance)
{
	common::ThreadLocal threadLocal(10 * 1024 * 1024);

	// Tee the whole log stream to a file when requested (before allocation tracking enables, so the open is untracked).
	if (!gLaunchOptions.logFile.empty())
	{
		common::EnableLogFile(gLaunchOptions.logFile);
	}

	LOG(kDefault, kInfo, "\nGame name: {}", game::kGameName);
	LOG(kDefault, kInfo, "Game version: {}", game::kiGameVersion);
	LOG(kDefault, kInfo, "Compiled with Windows 10 SDK version: {}.{}", VER_PRODUCTBUILD, VER_PRODUCTBUILD_QFE);
#if defined(BT_CLIENT)
	LOG(kDefault, kInfo, "Compiled with Vulkan SDK version: {}\n", VK_HEADER_VERSION);
	static_assert(VK_HEADER_VERSION >= 341, "Update the Vulkan SDK");
#endif

	if (!XMVerifyCPUSupport()) [[unlikely]]
	{
		throw std::runtime_error("Your CPU does not support SSE4.1 instructions");
	}

	// Disable CRT FMA3 auto-detection for deterministic math across CPUs
	if (_set_FMA3_enable(0) != 0) [[unlikely]]
	{
		throw std::runtime_error("Unable to disable FMA3");
	}

	if (SetProcessDPIAware() == 0) [[unlikely]]
	{
		throw std::runtime_error("SetProcessDPIAware failed");
	}

#if defined(BT_CLIENT)
	// Cursor
	sHcursorArrow = LoadCursor(nullptr, IDC_ARROW);
	sHcursorCrosshair = LoadCursor(nullptr, IDC_CROSS);
	common::ScopedLambda destroyCursor([]()
	{
		DestroyCursor(sHcursorArrow);
		DestroyCursor(sHcursorCrosshair);
	});
#endif // BT_CLIENT

#if defined(BT_CLIENT)
	// Save one core for the main thread, and one core for the render thread
	int64_t iBackgroundThreadCount = std::max(1ll, common::HardwareCoreCount() - 1 - 1);
#else
	// Server has no render thread
	int64_t iBackgroundThreadCount = std::max(1ll, common::HardwareCoreCount() - 1);
#endif
	auto pMultithreading = std::make_unique<common::Multithreading>(iBackgroundThreadCount);

	// Profile
	auto pProfileManager = std::make_unique<game::ProfileManager>();

	// Start DxDiag reading in the background
	std::future<void> readDxDiag;
	if constexpr (kbDxDiag)
	{
		if (IsDebuggerPresent() == 0) [[likely]]
		{
			readDxDiag = std::async(std::launch::async, ReadDxDiag);
		}
	}

#if defined(BT_CLIENT)
	// Input
	auto pRawInputManager = std::make_unique<RawInputManager>();

	// Audio
	auto pAudioManager = std::make_unique<AudioManager>();
#endif

	// Network
	auto pNetworkManager = std::make_unique<NetworkManager>();

	// Agent command channel (loopback JSON control, drained on the main thread). Constructed after NetworkManager
	// so WSAStartup (via ENet init) is done. Dormant unless --agent-port is passed; RAII teardown in reverse order.
	std::unique_ptr<AgentCommandServer> pAgentCommandServer;
	common::ScopedLambda clearAgentCommandServer([]()
	{
		gpAgentCommandServer = nullptr;
	});
#if defined(BT_CLIENT)
	// Client-only synthetic-input + UI-registry layer, active alongside the agent channel. Ctors set their gp*
	// globals; RAII teardown (reverse order) nulls them before the command server tears down.
	std::unique_ptr<AgentUiRegistry> pAgentUiRegistry;
	std::unique_ptr<AgentInput> pAgentInput;
#endif
	if constexpr (kbAgent)
	{
		if (gLaunchOptions.iAgentPort != 0)
		{
			try
			{
				pAgentCommandServer = std::make_unique<AgentCommandServer>(gLaunchOptions.iAgentPort);
			}
			catch (const AgentCommandServer::StartupException&)
			{
				// Ctor already logged the concrete error. Fail fast so no uncontrollable agent-launched process lingers.
				return;
			}
			gpAgentCommandServer = pAgentCommandServer.get();
#if defined(BT_CLIENT)
			pAgentUiRegistry = std::make_unique<AgentUiRegistry>();
			pAgentInput = std::make_unique<AgentInput>();
#endif
		}
	}

	// Register class
	WNDCLASSEX wndClassEx
	{
		.cbSize = sizeof(wndClassEx),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WndProc,
		.cbClsExtra = 0,
		.cbWndExtra = 0,
		.hInstance = hinstance,
		.hIcon = LoadIcon(nullptr, IDI_APPLICATION),
		.hCursor = nullptr,
		.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)),
		.lpszMenuName = nullptr,
		.lpszClassName = game::kGameName.data(),
		.hIconSm = LoadIcon(nullptr, IDI_APPLICATION),
	};
	ATOM atom = RegisterClassEx(&wndClassEx);
	if (atom == 0)
	{
		throw std::runtime_error("RegisterClassEx failed");
	}
	common::ScopedLambda unregisterClass([&hinstance]()
	{
		LOG(kDefault, kDebug, "Unregister class");
		UnregisterClass(game::kGameName.data(), hinstance);
	});

	// Setup window rect & matrices
#if defined(BT_CLIENT)
	game::LoadGraphicsSettings();
	gWantedFramebufferExtent2D = SetupWindow(WantedFullscreen(), sWindowStyle, sWindowRect);
#else
	LONG iWindowStyle = WS_POPUP;
	RECT windowRect {};
	SystemParametersInfo(SPI_GETWORKAREA, 0, &windowRect, 0);
#endif // BT_CLIENT

	// Create window
#if defined(BT_CLIENT)
	sHwnd = CreateWindow(game::kGameName.data(), game::kGameName.data(), sWindowStyle, sWindowRect.left, sWindowRect.top, sWindowRect.right - sWindowRect.left, sWindowRect.bottom - sWindowRect.top, nullptr, nullptr, hinstance, nullptr);
#else
	sHwnd = CreateWindow(game::kGameName.data(), game::kGameName.data(), iWindowStyle, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, nullptr, nullptr, hinstance, nullptr);
#endif
	if (sHwnd == nullptr)
	{
		throw std::runtime_error("CreateWindow failed");
	}
	common::ScopedLambda destroyWindow([]()
	{
		ProcessMessages();

		if (sHwnd != nullptr)
		{
			LOG(kDefault, kDebug, "Destroy window");
			DestroyWindow(sHwnd);
			sHwnd = nullptr;
		}
	});

#if defined(BT_CLIENT)
	// Load settings
	game::LoadSoundSettings();
	game::LoadGameSettings();

	// Create terrain collision data (before Graphics, which creates Islands that reads beach elevation)
	auto pIslandTerrain = std::make_unique<IslandTerrain>();

	// Wait for islands to load and initialize heightmaps before Graphics ctor. Terrain mesh CPU
	// slices are reclaimed immediately afterward; Graphics records its stable empty arena at boot.
	gpProfileManager->BootStart(kBootTimerWaitForIslands);
	gpIslandTerrain->WaitForElevationMaps(gBaseHeight.Get() - game::kfPlayerRadius - game::kfPushMargin);
	gpProfileManager->BootStop(kBootTimerWaitForIslands);

	// Register tweaks sections before the Graphics ctor builds ImGuiManager
	RegisterEngineTweakSections();
	game::RegisterGameTweakSections();

	// Initialize graphics
	gpProfileManager->BootStart(kBootTimerVulkan);
	auto pGraphics = std::make_unique<Graphics>(hinstance, sHwnd);

	// Load game
	auto pCamera = std::make_unique<game::Camera>();
	auto pGame = std::make_unique<game::Game>();

	// Input
	auto pInput = std::make_unique<game::Input>();
	game::gpInput = pInput.get();

	// Load tweaks settings (requires both Game and ImGuiManager)
	game::LoadTweaksSettings();

	// Load persistent client state (focused fleet/ship + zoom). Requires gpCamera and gpGame; both exist by here.
	game::LoadClientState();

	gpProfileManager->BootStop(kBootTimerVulkan);

	// Ensure priority textures are ready
	gpProfileManager->BootStart(kBootTimerWaitForPriorityTextures);
	gpTextureManager->WaitForTextures(TextureManager::kpPriorityTextures.pCrcs);
	gpProfileManager->BootStop(kBootTimerWaitForPriorityTextures);

	// Populate boot-time render interpolate for the single origin frame
	{
		// Heap: operator[] may insert default element
		ScopedSuppressAllocationTracking suppress;
		game::FrameInterpolate::AllocateAndCopy(pGame->mRenderInterpolates.try_emplace(kOriginCoord).first->second, pGame->RenderFrame(pGame->mClientGridCoord).interpolate);
	}
	game::gpCamera->Update(pGame->mRenderInterpolates.at(kOriginCoord));

	// Render and present all framebuffers, then show window
	gpProfileManager->BootStart(kBootTimerRenderPresent);
	std::vector<GridCoord> bootActiveCoords = {kOriginCoord};
	for (int64_t i = 0; i < static_cast<int64_t>(gpCommandBufferManager->mPerFramebufferCommandBuffers.size()); ++i)
	{
		gpGraphics->RenderGlobal(pGame->RenderFrame(pGame->mClientGridCoord).interpolate.fCurrentTime);
		gpGraphics->RenderMainPresentAcquire(gpSwapchainManager->miFramebufferIndex, pGame->mRenderInterpolates, bootActiveCoords, kOriginCoord);
	}
	gpProfileManager->BootStop(kBootTimerRenderPresent);

	// Agent-mode launch stays minimized and must not steal focus from the user's active app
	ShowWindow(sHwnd, gLaunchOptions.iAgentPort != 0 ? SW_SHOWMINNOACTIVE : SW_SHOWDEFAULT);
#else
	// Server: create terrain collision data (no Graphics)
	auto pIslandTerrain = std::make_unique<IslandTerrain>();
	gpIslandTerrain->WaitForElevationMaps(gBaseHeight.Get() - game::kfPlayerRadius - game::kfPushMargin);

	auto pGame = std::make_unique<game::Game>();

	ShowWindow(sHwnd, gLaunchOptions.iAgentPort != 0 ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
#endif // BT_CLIENT
	common::ScopedLambda hideWindow([]()
	{
		LOG(kDefault, kDebug, "Hide window");
		ShowWindow(sHwnd, SW_HIDE);
	});
#if defined(BT_CLIENT)
	// Agent-mode launch must not steal focus from the user's active app
	if (gLaunchOptions.iAgentPort == 0)
	{
		SetForegroundWindow(sHwnd);
		BringWindowToTop(sHwnd);
		SetFocus(sHwnd);
	}
	else
	{
		// Agent-mode clients boot silent; focus gain resumes.
		gpAudioManager->Suspend();
	}
#else
	if (gLaunchOptions.iAgentPort == 0)
	{
		SetFocus(sHwnd);
	}
#endif
	ProcessMessages();

	gpProfileManager->BootLog();

	EnableAllocationTracking(true);
	pGame->mTimeStep.mRealTime.Reset();

	LOG(kDefault, kInfo, "\nEnter main loop");
	LogIndent(1);
	while (true)
	{
		gpProfileManager->CpuStart(kCpuTimerMessagesAndInput);

		// Process Windows messages
		[[maybe_unused]] bool bLostFocus = ProcessMessages();
		if (sbQuit) [[unlikely]]
		{
			break;
		}

#if defined(BT_CLIENT)
		// Reconcile a pending fullscreen toggle here, in the main loop only — never from the teardown / boot
		// ProcessMessages() calls, so a mid-shutdown mismatch can't SetWindowPos a dying window. Read through
		// WantedFullscreen() so the --windowed / agent overrides fold in and gFullscreen is never mutated.
		bool bWantedFullscreen = WantedFullscreen();
		bool bIsFullscreen = (sWindowStyle & WS_POPUP) != 0;
		if (bIsFullscreen != bWantedFullscreen)
		{
			SetupWindow(bWantedFullscreen, sWindowStyle, sWindowRect);
			SetWindowLongPtr(sHwnd, GWL_STYLE, sWindowStyle);
			// SWP_NOZORDER | SWP_NOACTIVATE: the agent fullscreen command reaches this path, and the harness must never steal foreground focus.
			SetWindowPos(sHwnd, nullptr, sWindowRect.left, sWindowRect.top, sWindowRect.right - sWindowRect.left, sWindowRect.bottom - sWindowRect.top, SWP_NOZORDER | SWP_NOACTIVATE);
		}

		// Drain agent commands before input so injected input scripts (later harness plans) act on the same frame.
		// The server drains in GameBase::ServerUpdate instead, matching the debug-control packet ordering.
		if (gpAgentCommandServer != nullptr) [[unlikely]]
		{
			gpAgentCommandServer->Drain();

			// Advance the active synthetic-input script one step before input/ImGui so its ImGui IO events and
			// RawInput-overlay state land in this frame (RawInputManager::Update runs later via ProcessInput).
			if (gpAgentInput != nullptr) [[unlikely]]
			{
				gpAgentInput->AdvanceFrame();
			}
		}
#endif

		// Input
#if defined(BT_CLIENT)
		game::MenuInput menuInput {};
		pGame->ProcessInput(bLostFocus, menuInput);
		if (pGame->mGameFlags & engine::GameFlags::kQuit) [[unlikely]]
		{
			break;
		}
#endif

		gpProfileManager->CpuStop(kCpuTimerMessagesAndInput);

#if defined(BT_CLIENT)
		pGame->ClientUpdate();
#else
		pGame->ServerUpdate();
#endif // BT_CLIENT

#if defined(BT_CLIENT)
		try
		{
			gpTextureUploadManager->RethrowException();
			pGame->Render();
		}
		catch (DeviceLostException& rDeviceLostException)
		{
			LOG(kDefault, kDebug, "Caught rDeviceLostException: {}", rDeviceLostException.what());
			pGraphics.reset();
			pGraphics = std::make_unique<Graphics>(hinstance, sHwnd);
		}

		// Audio update
		auto audioCoordIt = pGame->mCoordFrames.find(game::gpGame->mClientGridCoord);
		pAudioManager->Update(audioCoordIt != pGame->mCoordFrames.end() && audioCoordIt->second.iSnapshotCount > 0 ? &pGame->RenderFrame(game::gpGame->mClientGridCoord) : nullptr);
#else
		{
			// Heap: Win32 InvalidateRect may trigger internal GDI allocations
			ScopedSuppressAllocationTracking suppress;
			game::ServerUpdateDisplayStats();

			// Throttle full-window GDI repaint to a fraction of the tick rate — paint cost dwarfs stat aggregation, and the window shows only coarse stats/map. Clicks still repaint immediately via WM_LBUTTONDOWN.
			static constexpr int64_t kiServerDisplayRepaintTicks = 8;
			static int64_t siServerDisplayRepaintCounter = 0;
			if (++siServerDisplayRepaintCounter >= kiServerDisplayRepaintTicks)
			{
				siServerDisplayRepaintCounter = 0;

				// Skip the repaint when it would blit nothing new: a minimized/hidden window paints offscreen for no benefit, and a visible window whose displayed stats/map are unchanged need not repaint an identical frame. Clicks still repaint immediately via WM_LBUTTONDOWN.
				// Keep a slow heartbeat while visible-but-unchanged so the free-running tick/timer text (deliberately outside the content hash) stays visibly alive instead of reading as a hung server.
				static constexpr int64_t kiServerDisplayHeartbeatWindows = 4; // 4 x 8-tick windows = 1 Hz at the 32 Hz tick rate
				static int64_t siServerDisplayHeartbeatCounter = 0;
				if (!IsIconic(sHwnd) && IsWindowVisible(sHwnd))
				{
					bool bHeartbeat = ++siServerDisplayHeartbeatCounter >= kiServerDisplayHeartbeatWindows;
					if (game::ServerDisplayContentChanged() || bHeartbeat)
					{
						siServerDisplayHeartbeatCounter = 0;
						InvalidateRect(sHwnd, nullptr, FALSE);
					}
				}
			}
		}
#endif // BT_CLIENT
	}
	LogIndent(-1);
	LOG(kDefault, kInfo, "Exit main loop\n\n");

	EnableAllocationTracking(false);

#if defined(BT_SERVER)
	pGame->mGameSaveLoad.Autosave();
#endif

#if defined(BT_CLIENT)
	// Save settings
	game::SaveTweaksSettings();
	game::SaveSoundSettings();
	game::SaveGraphicsSettings();
	game::SaveGameSettings();
#endif

	PostQuitMessage(0);
	ProcessMessages();
}

#if defined(BT_CLIENT)
static int64_t siMonitorCount = 0;
static bool sbUseCurrentRect = false;

void FindMonitor(bool bUseCurrentRect)
{
	sHmonitor = nullptr;
	sbUseCurrentRect = bUseCurrentRect;

	if (sbUseCurrentRect)
	{
		GetWindowRect(sHwnd, &sWindowRect);
		LOG(kDefault, kDebug, "Window left top: {}, {}", sWindowRect.left, sWindowRect.top);
	}

	LOG(kDefault, kDebug, "Monitors:");
	siMonitorCount = 0;
	EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hmonitor, [[maybe_unused]] HDC hdc, [[maybe_unused]] LPRECT lprect, [[maybe_unused]] LPARAM lparam) -> BOOL
	{
		MONITORINFO monitorinfo;
		monitorinfo.cbSize = sizeof(monitorinfo);
		GetMonitorInfo(hmonitor, &monitorinfo);
		bool bPrimary = (monitorinfo.dwFlags & MONITORINFOF_PRIMARY) != 0;
		bool bRectIsInMonitor = sWindowRect.left >= monitorinfo.rcMonitor.left && sWindowRect.left <= monitorinfo.rcMonitor.right && sWindowRect.top >= monitorinfo.rcMonitor.top && sWindowRect.top <= monitorinfo.rcMonitor.bottom;

		[[maybe_unused]] LONG iWidth = monitorinfo.rcMonitor.right - monitorinfo.rcMonitor.left;
		[[maybe_unused]] LONG iHeight = monitorinfo.rcMonitor.bottom - monitorinfo.rcMonitor.top;
		LOG(kDefault, kDebug, "  {}: {} x {}{}{}", siMonitorCount++, iWidth, iHeight, bPrimary ? " (Primary)" : "", bRectIsInMonitor ? " (Monitor)" : "");

		if (sHmonitor == nullptr || (sbUseCurrentRect && bRectIsInMonitor) || (!sbUseCurrentRect && bPrimary))
		{
			sHmonitor = hmonitor;
			sMonitorInfo = monitorinfo;
		}

		return TRUE;
	}, 0);
	LOG(kDefault, kDebug, "");
}

VkExtent2D SetupWindow(bool bFullscreen, LONG& riWindowStyle, RECT& rWindowRect)
{
	if (sHwnd == nullptr)
	{
		riWindowStyle = 0;
	}
	else
	{
		riWindowStyle = GetWindowLong(sHwnd, GWL_STYLE) & ~(WS_OVERLAPPEDWINDOW | WS_POPUP);
	}

	FindMonitor(sHwnd != nullptr);

	if (bFullscreen)
	{
		riWindowStyle |= WS_POPUP;
		rWindowRect = sMonitorInfo.rcMonitor;
	}
	else
	{
		riWindowStyle |= WS_OVERLAPPEDWINDOW;

		LONG iX = common::RoundUp<LONG, 8>(static_cast<LONG>(0.05f * static_cast<float>(sMonitorInfo.rcMonitor.right)));
		LONG iY = common::RoundUp<LONG, 8>(static_cast<LONG>(0.05f * static_cast<float>(sMonitorInfo.rcMonitor.bottom)));

		if (gLaunchOptions.windowedExtent.width != 0 && gLaunchOptions.windowedExtent.height != 0)
		{
			// Reproducible agent capture geometry: use the requested client size (multiple-of-8 rounded, matching
			// the default inset path), anchored at the monitor's top-left inset.
			LONG iClientWidth = common::RoundUp<LONG, 8>(static_cast<LONG>(gLaunchOptions.windowedExtent.width));
			LONG iClientHeight = common::RoundUp<LONG, 8>(static_cast<LONG>(gLaunchOptions.windowedExtent.height));
			rWindowRect.left = sMonitorInfo.rcMonitor.left + iX;
			rWindowRect.top = sMonitorInfo.rcMonitor.top + iY;
			rWindowRect.right = rWindowRect.left + iClientWidth;
			rWindowRect.bottom = rWindowRect.top + iClientHeight;
		}
		else
		{
			rWindowRect.left = sMonitorInfo.rcMonitor.left + iX;
			rWindowRect.right = sMonitorInfo.rcMonitor.right - iX;
			rWindowRect.top = sMonitorInfo.rcMonitor.top + iY;
			rWindowRect.bottom = sMonitorInfo.rcMonitor.bottom - iY;
		}
	}

	LONG iFramebufferWidth = rWindowRect.right - rWindowRect.left;
	LONG iFramebufferHeight = rWindowRect.bottom - rWindowRect.top;
	LOG(kDefault, kDebug, "Set {} window {} x {} at ({}, {})", (riWindowStyle & WS_OVERLAPPEDWINDOW) != 0 ? "WS_OVERLAPPEDWINDOW" : "WS_POPUP", iFramebufferWidth, iFramebufferHeight, rWindowRect.left, rWindowRect.top);

	if ((riWindowStyle & WS_OVERLAPPEDWINDOW) != 0)
	{
		AdjustWindowRect(&rWindowRect, riWindowStyle, FALSE);
	}

	return {static_cast<uint32_t>(iFramebufferWidth), static_cast<uint32_t>(iFramebufferHeight)};
}
#endif // BT_CLIENT

bool ProcessMessages()
{
	// Process messages with PeekMessage() which doesn't block
	MSG msg {};
	bool bHasMessage = PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) == TRUE;
	while (bHasMessage)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		bHasMessage = PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) == TRUE;
	}

	return !sbHasFocus;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#if defined(BT_CLIENT)
	// Game handles cursor when ImGui doesn't want the mouse. Guard GetIO(): the ImGui context can be destroyed across a
	// multi-frame deferred swapchain recreate (minimized client), and this fires on the restore frame over the client area.
	if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT && ImGui::GetCurrentContext() != nullptr && !ImGui::GetIO().WantCaptureMouse)
	{
		SetCursor(game::gpGame->ShouldUseCrosshair() ? sHcursorCrosshair : sHcursorArrow);
		return TRUE;
	}

	const bool bInputSuppressed = PhysicalInputSuppressed();

	// When suppressed, keep the ImGui Win32 backend running for lifecycle bookkeeping (focus, tracking) but starve it
	// of physical input messages (mouse/keyboard/char/wheel ranges) so real human activity never becomes ImGui IO.
	// Non-client mouse messages (WM_NCMOUSEMOVE etc.) are deliberately NOT added to the bypass ranges: the backend may
	// queue a physical pos from them, but ImGuiManager::Prepare's re-pin/sentinel is always the last mouse-pos event
	// before NewFrame, so gating them is unnecessary and is not done.
	const bool bInputMessage = (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
		|| (message >= WM_KEYFIRST && message <= WM_KEYLAST);
	if (!(bInputSuppressed && bInputMessage))
	{
		if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam) != 0)
		{
			return TRUE;
		}
	}

	switch (message)
	{
		// WM_ACTIVATEAPP stays ungated even when suppressed — DirectXTK Mouse focus bookkeeping, not a physical input feed.
		case WM_ACTIVATEAPP:
			Mouse::ProcessMessage(message, wParam, lParam);
			break;

		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MOUSEWHEEL:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		case WM_MOUSEHOVER:
			if (!bInputSuppressed)
			{
				Mouse::ProcessMessage(message, wParam, lParam);
			}
			break;

		default:
			break;
	}
#endif // BT_CLIENT

	switch (message)
	{
#if defined(BT_SERVER)
		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
		{
			// Heap: GDI painting creates/destroys kernel objects that may trigger CRT allocations
			ScopedSuppressAllocationTracking suppress;
			game::PaintServerDisplay(hWnd);
			return 0;
		}

		case WM_KEYDOWN:
		{
			if constexpr (kbDebugInput)
			{
				if (wParam == VK_F4)
				{
					sbQuit = true;
				}
			}
			return 0;
		}

		case WM_LBUTTONDOWN:
		{
			game::HandleServerClick(hWnd, LOWORD(lParam), HIWORD(lParam));
			InvalidateRect(hWnd, nullptr, FALSE);
			return 0;
		}
#endif // BT_SERVER

		case WM_SYSCOMMAND:
		{
			// Suppress system commands that enter modal loops and block the main thread
			WORD wSysCommand = wParam & 0xFFF0;
			if (wSysCommand == SC_KEYMENU
#if defined(BT_SERVER)
				|| wSysCommand == SC_MOVE
				|| wSysCommand == SC_SIZE
				|| wSysCommand == SC_MAXIMIZE
				|| wSysCommand == SC_RESTORE
#endif
				)
			{
				return 0;
			}
			break;
		}

		case WM_SETFOCUS:
		{
			LOG(kDefault, kInfo, "WM_SETFOCUS");

			if (!sbHasFocus)
			{
				sbHasFocus = true;

#if defined(BT_CLIENT)
				SetCursor(game::gpGame->ShouldUseCrosshair() ? sHcursorCrosshair : sHcursorArrow);

				gpAudioManager->Resume();

				gpRawInputManager->UpdateFocus(true, sHwnd);
#endif // BT_CLIENT
			}

			break;
		}

		case WM_KILLFOCUS:
		{
			LOG(kDefault, kInfo, "WM_KILLFOCUS");

			if (sbHasFocus)
			{
				sbHasFocus = false;

#if defined(BT_CLIENT)
				gpAudioManager->Suspend();

				gpRawInputManager->UpdateFocus(false, sHwnd);
#endif // BT_CLIENT
			}

			break;
		}

		case WM_INPUT:
		{
#if defined(BT_CLIENT)
			gpRawInputManager->HandleRawInput(lParam);
#endif
			break; // WM_INPUT must reach DefWindowProc so the system can free the RAWINPUT handle
		}

		case WM_SIZE:
		{
#if defined(BT_CLIENT)
			gWantedFramebufferExtent2D = {static_cast<uint32_t>(lParam) & 0xFFFF, static_cast<uint32_t>(lParam) >> 16};
			LOG(kDefault, kDebug, "WM_SIZE: {} x {}", gWantedFramebufferExtent2D.width, gWantedFramebufferExtent2D.height);
#endif
			break;
		}

		case WM_CLOSE:
		{
			LOG(kDefault, kDebug, "WM_CLOSE");
			sbQuit = true;
			return 0;
		}

		case WM_DESTROY:
		{
			LOG(kDefault, kDebug, "WM_DESTROY");
			sbQuit = true;
			sHwnd = nullptr;
			break;
		}

		default:
		{
			break;
		}
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

} // namespace engine

int WINAPI wWinMain(_In_ HINSTANCE hInstance, [[maybe_unused]] _In_opt_ HINSTANCE hPrevInstance, [[maybe_unused]] _In_ LPWSTR lpCmdLine, [[maybe_unused]] _In_ int nShowCmd)
{
	if (!engine::ParseLaunchOptions())
	{
		return 0; // Fatal launch-option error (e.g. --agent-port out of range); already logged kError.
	}

	// Prevent multiple instances from running simultaneously
	std::unique_ptr<void, decltype(&CloseHandle)> pMutex(nullptr, &CloseHandle);
	if constexpr (kbSingleInstance)
	{
		HANDLE hMutex = CreateMutex(nullptr, TRUE, "BrokenEngineSandboxServer");
		pMutex.reset(hMutex);
		if (GetLastError() == ERROR_ALREADY_EXISTS)
		{
			// An agent-launched instance must never block on a modal dialog — fail fast so AgentHarness sees the exit.
			if (engine::gLaunchOptions.iAgentPort != 0)
			{
				LOG(kDefault, kError, "Another instance is already running; --agent-port launch aborting");
				return 0;
			}

			MessageBox(nullptr, "Server is already running.", game::kGameName.data(), MB_OK | MB_SYSTEMMODAL);
			return 0;
		}
	}

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

#if defined(BT_CLIENT)
	// Windows::Foundation::Initialize is required for XAudio2 (and possibly gamepads as well)
	HRESULT hresult = Windows::Foundation::Initialize(RO_INIT_MULTITHREADED);
	if (hresult != S_OK) [[unlikely]]
	{
		MessageBox(nullptr, common::HresultToString(hresult).data(), "Windows::Foundation::Initialize", MB_OK | MB_SYSTEMMODAL);
		return 0;
	}
#endif

#if defined(BT_CLIENT)
	auto pTextureUploadManager = std::make_unique<engine::TextureUploadManager>();
#endif
	auto pFileManager = std::make_unique<engine::FileManager>();

	if (IsDebuggerPresent() != 0) [[unlikely]]
	{
		engine::MainThread(hInstance);
	}
	else [[likely]]
	{
		try
		{
			engine::MainThread(hInstance);
		}
		catch (const std::exception& rException)
		{
			engine::HandleException(&rException);
		}
		catch (...)
		{
			engine::HandleException();
		}
	}

#if defined(BT_CLIENT)
	LOG(kDefault, kDebug, "Windows foundation uninitialize");
	Windows::Foundation::Uninitialize();
#endif

	return 0;
}
