#include "Memory/MemoryManager.h"

#include "CrashReport.h"
#include "Game.h"
#include "Profile/ProfileManager.h"

namespace engine
{

static bool sbQuit = false;

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
#endif

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

#if defined(BT_CLIENT)
void FindMonitor(bool bUseCurrentRect);
VkExtent2D SetupWindow(bool bFullscreen, LONG& riWindowStyle, RECT& rWindowRect);
#endif
bool ProcessMessages();

void MainThread(HINSTANCE hinstance)
{
	common::ThreadLocal threadLocal(10 * 1024 * 1024);

	Log("\nGame name: {}", game::kGameName);
	Log("Game version: {}", game::kiGameVersion);
	Log("Compiled with Windows 10 SDK version: {}.{}", VER_PRODUCTBUILD, VER_PRODUCTBUILD_QFE);
#if defined(BT_CLIENT)
	Log("Compiled with Vulkan SDK version: {}\n", VK_HEADER_VERSION);
	static_assert(VK_HEADER_VERSION >= 304, "Update the Vulkan SDK");
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
	giBackgroundThreadCount = std::max(1ll, common::HardwareCoreCount() - 1 - 1);
#else
	// Server has no render thread
	giBackgroundThreadCount = std::max(1ll, common::HardwareCoreCount() - 1);
#endif
	auto pMultithreading = std::make_unique<common::Multithreading>(giBackgroundThreadCount);

	// Profile
	auto pProfileManager = std::make_unique<game::ProfileManager>();

	// Start DxDiag reading in the background
	std::future<void> readDxDiag;
	if constexpr (kbEnableDxDiag)
	{
		if (!IsDebuggerPresent()) [[likely]]
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
		Log("Unregister class");
		UnregisterClass(game::kGameName.data(), hinstance);
	});

	// Setup window rect & matrices
#if defined(BT_CLIENT)
	gWantedFramebufferExtent2D = SetupWindow(gFullscreen.Get<bool>(), sWindowStyle, sWindowRect);

	if (gWantedFramebufferExtent2D.height < 2160)
	{
		gSampleCount.Set<VkSampleCountFlagBits>(VK_SAMPLE_COUNT_4_BIT);
	}
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
#if defined(BT_CLIENT)
	pRawInputManager->mHwnd = sHwnd;
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
			Log("Destroy window");
			DestroyWindow(sHwnd);
			sHwnd = nullptr;
		}
	});

#if defined(BT_CLIENT)
	// Load settings
	game::Game::LoadSoundSettings();

	// Create terrain collision data (before Graphics, which creates Islands that reads beach elevation)
	auto pIslandTerrain = std::make_unique<IslandTerrain>();

	// Initialize graphics
	gpProfileManager->BootStart(kBootTimerVulkan);
	auto pGraphics = std::make_unique<Graphics>(hinstance, sHwnd);

	// Wait for islands to load and initialize heightmaps
	gpProfileManager->BootStart(kBootTimerWaitForIslands);
	gpIslandTerrain->WaitForElevationMaps();
	gpProfileManager->BootStop(kBootTimerWaitForIslands);

	// Load game
	auto pCamera = std::make_unique<game::Camera>();
	auto pGame = std::make_unique<game::Game>();

	// Input
	auto pInput = std::make_unique<game::Input>();
	game::gpInput = pInput.get();

	gpProfileManager->BootStop(kBootTimerVulkan);

	// Ensure priority textures are ready
	gpProfileManager->BootStart(kBootTimerWaitForPriorityTextures);
	gpTextureManager->WaitForTextures(TextureManager::smPriorityTextures);
	gpProfileManager->BootStop(kBootTimerWaitForPriorityTextures);

	// Populate boot-time render interpolate for the single origin frame
	{
		// Heap: operator[] may insert default element
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		game::FrameInterpolate::AllocateAndCopy(gpGraphics->mRenderInterpolates[kOriginCoord], pGame->CurrentFrame(pGame->mHumanGridCoord).interpolate);
	}
	game::gpCamera->Update(gpGraphics->mRenderInterpolates.at(kOriginCoord));

	// Render and present all framebuffers, then show window
	gpProfileManager->BootStart(kBootTimerRenderPresent);
	std::vector<GridCoord> bootActiveCoords = {kOriginCoord};
	for (int64_t i = 0; i < static_cast<int64_t>(gpCommandBufferManager->mPerFramebufferCommandBuffers.size()); ++i)
	{
		gpGraphics->RenderGlobal(pGame->CurrentFrame(pGame->mHumanGridCoord), pGame->CurrentFrame(pGame->mHumanGridCoord).interpolate.fCurrentTime);
		gpGraphics->RenderMainPresentAcquire(gpSwapchainManager->miFramebufferIndex, gpGraphics->mRenderInterpolates, bootActiveCoords, kOriginCoord);
	}
	gpProfileManager->BootStop(kBootTimerRenderPresent);

	ShowWindow(sHwnd, SW_SHOWDEFAULT);
#else
	// Server: create terrain collision data (no Graphics)
	auto pIslandTerrain = std::make_unique<IslandTerrain>();
	gpIslandTerrain->WaitForElevationMaps();

	auto pGame = std::make_unique<game::Game>();

	auto pServerNetwork = std::make_unique<ServerNetwork>(kuiDefaultPort);

	ShowWindow(sHwnd, SW_SHOWMAXIMIZED);
#endif // BT_CLIENT
	common::ScopedLambda hideWindow([]()
	{
		Log("Hide window");
		ShowWindow(sHwnd, SW_HIDE);
	});
	BringWindowToTop(sHwnd);
	SetFocus(sHwnd);
	ProcessMessages();

	gpProfileManager->BootLog();

	EnableAllocationTracking(true);
	pGame->mTimeStep.mRealTime.Reset();

	Log("\nEnter main loop");
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

		// Input
		game::MenuInput menuInput {};
#if defined(BT_CLIENT)
		pGame->ProcessInput(bLostFocus, menuInput);
		if (pGame->mGameFlags & engine::GameFlags::kQuit) [[unlikely]]
		{
			break;
		}
#endif

		gpProfileManager->CpuStop(kCpuTimerMessagesAndInput, false);

#if defined(BT_CLIENT)
		pGame->UpdateClient();
#else
		pGame->UpdateServer(menuInput);
#endif // BT_CLIENT

#if defined(BT_CLIENT)
		try
		{
			pGame->Render();
		}
		catch (DeviceLostException& rDeviceLostException)
		{
			Log("Caught rDeviceLostException: {}", rDeviceLostException.what());
			pGraphics.reset();
			pGraphics = std::make_unique<Graphics>(hinstance, sHwnd);
		}

		// Audio update
		pAudioManager->Update(pGame->RenderFrame(game::gpGame->mHumanGridCoord));
#else
		{
			// Heap: Win32 InvalidateRect may trigger internal GDI allocations
			ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
			UpdateServerDisplayStats();
			InvalidateRect(sHwnd, nullptr, FALSE);
		}
#endif // BT_CLIENT
	}
	LogIndent(-1);
	Log("Exit main loop\n\n");

	EnableAllocationTracking(false);

#if defined(BT_CLIENT)
	// Save settings
	game::Game::SaveSoundSettings();
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
		Log("Window left top: {}, {}", sWindowRect.left, sWindowRect.top);
	}

	Log("Monitors:");
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
		Log("  {}: {} x {}{}{}", siMonitorCount++, iWidth, iHeight, bPrimary ? " (Primary)" : "", bRectIsInMonitor ? " (Monitor)" : "");

		if (sHmonitor == nullptr || (sbUseCurrentRect && bRectIsInMonitor) || (!sbUseCurrentRect && bPrimary))
		{
			sHmonitor = hmonitor;
			sMonitorInfo = monitorinfo;
		}

		return TRUE;
	}, 0);
	Log("");
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
		rWindowRect.left = sMonitorInfo.rcMonitor.left + iX;
		rWindowRect.right = sMonitorInfo.rcMonitor.right - iX;
		rWindowRect.top = sMonitorInfo.rcMonitor.top + iY;
		rWindowRect.bottom = sMonitorInfo.rcMonitor.bottom - iY;
	}

	LONG iFramebufferWidth = rWindowRect.right - rWindowRect.left;
	LONG iFramebufferHeight = rWindowRect.bottom - rWindowRect.top;
	Log("Set {} window {} x {} at ({}, {})", (riWindowStyle & WS_OVERLAPPEDWINDOW) != 0 ? "WS_OVERLAPPEDWINDOW" : "WS_POPUP", iFramebufferWidth, iFramebufferHeight, rWindowRect.left, rWindowRect.top);

	if ((riWindowStyle & WS_OVERLAPPEDWINDOW) != 0)
	{
		AdjustWindowRect(&rWindowRect, riWindowStyle, FALSE);
	}

	return {static_cast<uint32_t>(iFramebufferWidth), static_cast<uint32_t>(iFramebufferHeight)};
}
#endif // BT_CLIENT

bool ProcessMessages()
{
#if defined(BT_CLIENT)
	// Handle fullscreen toggle
	bool bWantedFullscreen = gFullscreen.Get<bool>();
	bool bIsFullscreen = (sWindowStyle & WS_POPUP) != 0;
	if (bIsFullscreen != bWantedFullscreen)
	{
		SetupWindow(bWantedFullscreen, sWindowStyle, sWindowRect);
		SetWindowLongPtr(sHwnd, GWL_STYLE, sWindowStyle);
		SetWindowPos(sHwnd, nullptr, sWindowRect.left, sWindowRect.top, sWindowRect.right - sWindowRect.left, sWindowRect.bottom - sWindowRect.top, 0);
	}
#endif // BT_CLIENT

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
	// Game handles cursor when ImGui doesn't want the mouse
	if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT && !ImGui::GetIO().WantCaptureMouse)
	{
		SetCursor(game::gpGame->ShouldUseCrosshair() ? sHcursorCrosshair : sHcursorArrow);
		return TRUE;
	}

	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	{
		return true;
	}

	switch (message)
	{
		case WM_ACTIVATEAPP:
		case WM_INPUT:
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
			Mouse::ProcessMessage(message, wParam, lParam);
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
			ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
			PaintServerDisplay(hWnd);
			return 0;
		}

		case WM_KEYDOWN:
		{
			if constexpr (kbEnableDebugInput)
			{
				if (wParam == VK_F4)
				{
					sbQuit = true;
				}
			}
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
			Log("WM_SETFOCUS");

			if (!sbHasFocus)
			{
				sbHasFocus = true;

#if defined(BT_CLIENT)
				SetCursor(game::gpGame->ShouldUseCrosshair() ? sHcursorCrosshair : sHcursorArrow);

				if (gpAudioManager->mpAudioEngine != nullptr)
				{
					gpAudioManager->mpAudioEngine->Resume();
				}

				gpRawInputManager->UpdateFocus(true, sHwnd);
#endif // BT_CLIENT
			}

			break;
		}

		case WM_KILLFOCUS:
		{
			Log("WM_KILLFOCUS");

			if (sbHasFocus)
			{
				sbHasFocus = false;

#if defined(BT_CLIENT)
				if (gpAudioManager->mpAudioEngine != nullptr)
				{
					gpAudioManager->mpAudioEngine->Suspend();
				}

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
			return 0;
		}

		case WM_SIZE:
		{
#if defined(BT_CLIENT)
			gWantedFramebufferExtent2D = {static_cast<uint32_t>(lParam) & 0xFFFF, static_cast<uint32_t>(lParam) >> 16};
			Log("WM_SIZE: {} x {}", gWantedFramebufferExtent2D.width, gWantedFramebufferExtent2D.height);
#endif
			break;
		}

		case WM_CLOSE:
		case WM_QUIT:
		{
			Log("WM_CLOSE or WM_QUIT");
			sbQuit = true;
			return 0;
		}

		case WM_DESTROY:
		{
			Log("WM_DESTROY");
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
	// Prevent multiple instances from running simultaneously
	std::unique_ptr<void, decltype(&CloseHandle)> pMutex(nullptr, &CloseHandle);
	if constexpr (kbSingleInstance)
	{
		HANDLE hMutex = CreateMutex(nullptr, TRUE, "BrokenEngineSandboxServer");
		pMutex.reset(hMutex);
		if (GetLastError() == ERROR_ALREADY_EXISTS)
		{
			MessageBox(nullptr, "Server is already running.", game::kGameName.data(), MB_OK | MB_SYSTEMMODAL);
			return 0;
		}
	}

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	// Windows::Foundation::Initialize is required for XAudio2 (and possibly gampads as well)
	HRESULT hresult = Windows::Foundation::Initialize(RO_INIT_MULTITHREADED);
	if (hresult != S_OK) [[unlikely]]
	{
		MessageBox(nullptr, common::HresultToString(hresult).data(), "Windows::Foundation::Initialize", MB_OK | MB_SYSTEMMODAL);
		return 0;
	}

#if defined(BT_CLIENT)
	auto pTextureUploadManager = std::make_unique<engine::TextureUploadManager>();
#endif
	auto pFileManager = std::make_unique<engine::FileManager>();

	if (IsDebuggerPresent()) [[unlikely]]
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

	Log("Windows foundation uninitialize");
	Windows::Foundation::Uninitialize();

	return 0;
}
