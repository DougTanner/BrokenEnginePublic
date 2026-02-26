#ifdef BT_CLIENT
#include "Audio/AudioManager.h"
#include "Graphics/Graphics.h"
#include "Graphics/GraphicsUtils.h"
#include "Graphics/Managers/CommandBufferManager.h"
#include "Graphics/Managers/SwapchainManager.h"
#include "Graphics/Managers/TextureManager.h"
#include "Graphics/Managers/TextureUploadManager.h"
#endif
#include "File/FileManager.h"
#include "Network/NetworkManager.h"
#ifdef BT_SERVER
#include "Network/NetworkServer.h"
#endif
#include "Graphics/Islands.h"
#include "Input/RawInputManager.h"
#include "Memory/MemoryManager.h"
#include "Multithreading.h"
#include "Profile/ProfileManagerBase.h"
#include "Server/ServerDisplay.h"

#include "Game.h"
#include "Profile/ProfileManager.h"

namespace engine
{

static bool sbQuit = false;

#ifdef BT_CLIENT
static HCURSOR sHcursorArrow = nullptr;
static HCURSOR sHcursorCrosshair = nullptr;
static bool sbUseCrosshair = false;
#endif

static HWND sHwnd = nullptr;
static HMONITOR sHmonitor = nullptr;
static MONITORINFO sMonitorInfo {};

static bool sbHasFocus = false;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

#ifdef BT_CLIENT
void FindMonitor(bool bUseCurrentRect);
VkExtent2D SetupWindow(bool bFullscreen, LONG& riWindowStyle, RECT& rWindowRect);
#endif
bool ProcessMessages(bool bIgnoreFocus = false);

static std::string sDxDiag;
void ReadDxDiag();

void MainThread(HINSTANCE hinstance)
{
	common::ThreadLocal threadLocal(10 * 1024 * 1024);

	auto pProfileManager = std::make_unique<game::ProfileManager>();

	// Save one core for the main thread, and one core for the graphics thread
	giBackgroundThreadCount = std::max(1ll, common::HardwareCoreCount() - 1 - 1);
	auto pMultithreading = std::make_unique<common::Multithreading>(giBackgroundThreadCount);

	if (!XMVerifyCPUSupport()) [[unlikely]]
	{
		throw std::runtime_error("Your CPU does not support SSE4.1 instructions");
	}

	if (SetProcessDPIAware() == 0) [[unlikely]]
	{
		throw std::runtime_error("SetProcessDPIAware failed");
	}

	// Disable CRT FMA3 auto-detection for deterministic math across CPUs
	_set_FMA3_enable(0);

	// Flush denormals and explicitly set round-to-nearest for deterministic FP math
	// https://docs.microsoft.com/en-us/windows/win32/dxmath/pg-xnamath-optimizing#denormals
	unsigned int uiCurrentState = 0;
	_controlfp_s(&uiCurrentState, _DN_FLUSH | _RC_NEAR, _MCW_DN | _MCW_RC);

	// DxDiag
	std::future<void> readDxDiag;
	if constexpr (kbEnableDxDiag)
	{
		if (!IsDebuggerPresent()) [[likely]]
		{
			readDxDiag = std::async(std::launch::async, ReadDxDiag);
		}
	}

	// Audio
#ifdef BT_CLIENT
	auto pAudioManager = std::make_unique<AudioManager>();
#endif

	// Network
	auto pNetworkManager = std::make_unique<NetworkManager>();

	Log("\nGame name: {}", game::kGameName);
	Log("Game version: {}", game::kiGameVersion);
	Log("Compiled with Windows 10 SDK version: {}.{}", VER_PRODUCTBUILD, VER_PRODUCTBUILD_QFE);
#ifdef BT_CLIENT
	Log("Compiled with Vulkan SDK version: {}\n", VK_HEADER_VERSION);
	static_assert(VK_HEADER_VERSION >= 304, "Update the Vulkan SDK");
#endif

#ifdef BT_CLIENT
	// Cursor
	sHcursorArrow = LoadCursor(nullptr, IDC_ARROW);
	sHcursorCrosshair = LoadCursor(nullptr, IDC_CROSS);
	common::ScopedLambda destroyCursor([]()
	{
		DestroyCursor(sHcursorArrow);
		DestroyCursor(sHcursorCrosshair);
	});
#endif

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

#ifdef BT_CLIENT
	// Setup window rect & matrices
	LONG iWindowStyle = 0;
	RECT windowRect {};
	gWantedFramebufferExtent2D = SetupWindow(gFullscreen.Get<bool>(), iWindowStyle, windowRect);

	if (gWantedFramebufferExtent2D.height < 2160)
	{
		gSampleCount.Set<VkSampleCountFlagBits>(VK_SAMPLE_COUNT_4_BIT);
	}
#else
	LONG iWindowStyle = WS_OVERLAPPEDWINDOW;
	RECT windowRect {0, 0, 600, 400};
#endif

	// Create window
#ifdef BT_CLIENT
	auto pRawInputManager = std::make_unique<RawInputManager>();
#endif
#ifdef BT_SERVER
	const char* pcWindowTitle = "Broken Engine Server";
#else
	const char* pcWindowTitle = game::kGameName.data();
#endif
	sHwnd = CreateWindow(game::kGameName.data(), pcWindowTitle, iWindowStyle, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, nullptr, nullptr, hinstance, nullptr);
#ifdef BT_CLIENT
	pRawInputManager->mHwnd = sHwnd;
#endif
	if (sHwnd == nullptr)
	{
		throw std::runtime_error("CreateWindow failed");
	}
	common::ScopedLambda destroyWindow([]()
	{
		ProcessMessages(true);

		if (sHwnd != nullptr)
		{
			Log("Destroy window");
			DestroyWindow(sHwnd);
			sHwnd = nullptr;
		}
	});

	// Load settings
#ifdef BT_CLIENT
	game::Game::LoadSoundSettings();
#endif

#ifdef BT_CLIENT
	// Initialize graphics
	gpProfileManager->BootStart(kBootTimerVulkan);
	auto pGraphics = std::make_unique<Graphics>(hinstance, sHwnd);

	// Wait for islands to load and initialize heightmaps
	gpProfileManager->BootStart(kBootTimerWaitForIslands);
	gpIslands->WaitForElevationMaps();
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
		ResetRealTime();
		gpGraphics->RenderGlobal(pGame->CurrentFrame(pGame->mHumanGridCoord), pGame->CurrentFrame(pGame->mHumanGridCoord).interpolate.fCurrentTime);
		gpGraphics->RenderMainPresentAcquire(gpSwapchainManager->miFramebufferIndex, gpGraphics->mRenderInterpolates, bootActiveCoords, kOriginCoord, pGame->CurrentFrames());
	}
	gpProfileManager->BootStop(kBootTimerRenderPresent);

	ShowWindow(sHwnd, SW_SHOWDEFAULT);
	common::ScopedLambda hideWindow([]()
	{
		Log("Hide window");
		ShowWindow(sHwnd, SW_HIDE);
	});
	BringWindowToTop(sHwnd);
	SetFocus(sHwnd);
	ProcessMessages(true);
#else
	// Server: create Islands independently (no Graphics)
	auto pIslands = std::make_unique<Islands>();
	gpIslands->WaitForElevationMaps();

	auto pGame = std::make_unique<game::Game>();

	auto pNetworkServer = std::make_unique<NetworkServer>(kuiDefaultPort);

	HANDLE hTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	common::ScopedLambda closeTimer([hTimer]()
	{
		CloseHandle(hTimer);
	});

	ShowWindow(sHwnd, SW_SHOWMAXIMIZED);
	SetFocus(sHwnd);
	ProcessMessages(true);
#endif

	gpProfileManager->BootLog();

	ScopedLogIndent scopedLogIndent;
	Log("\nEnter main loop");
	EnableAllocationTracking(true);
	ResetRealTime();

	while (true)
	{
		gpProfileManager->CpuStart(kCpuTimerMessagesAndInput);

#ifdef BT_CLIENT
		// Handle fullscreen toggle
		bool bWantedFullscreen = gFullscreen.Get<bool>();
		bool bIsFullscreen = (iWindowStyle & WS_POPUP) != 0;
		if (bIsFullscreen != bWantedFullscreen)
		{
			SetupWindow(bWantedFullscreen, iWindowStyle, windowRect);
			SetWindowLongPtr(sHwnd, GWL_STYLE, iWindowStyle);
			SetWindowPos(sHwnd, nullptr, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, 0);
		}
#endif

		// Process Windows messages
#ifdef BT_CLIENT
		bool bLostFocus = ProcessMessages();
#else
		bool bLostFocus = false;
#endif
		if (sbQuit) [[unlikely]]
		{
			break;
		}

		// Input
#ifdef BT_CLIENT
		pRawInputManager->Update(bLostFocus);
		if (pInput->UpdateMenuInput(pRawInputManager->mRawInput)) [[unlikely]]
		{
			break;
		}
#endif

		game::MenuInput menuInput =
#ifdef BT_CLIENT
			pInput->GetMenuInput();
#else
			{};
#endif

#ifdef BT_SERVER
		// Sleep until next physics tick
		{
			std::chrono::nanoseconds remainingNs = (game::kUpdateStepNs - pGame->mTimeStep.mUpdateRemainderNs) * pGame->mTimeStep.miTimeDivide / pGame->mTimeStep.miTimeMultiply;
			if (remainingNs > 0ns)
			{
				LARGE_INTEGER dueTime {};
				dueTime.QuadPart = -(remainingNs.count() / 100); // Negative = relative, 100ns units
				SetWaitableTimerEx(hTimer, &dueTime, 0, nullptr, nullptr, nullptr, 0);
				MsgWaitForMultipleObjects(1, &hTimer, FALSE, INFINITE, QS_ALLINPUT);
			}
		}
#endif

		bool bUpdateFrames = pGame->PreUpdate(menuInput, bLostFocus);
		if (pGame->mGameFlags & engine::GameFlags::kQuit) [[unlikely]]
		{
			break;
		}

		gpProfileManager->CpuStop(kCpuTimerMessagesAndInput, false);

#ifdef BT_CLIENT
		{
			// Heap: ENet polling, reconciliation, and network state processing
			ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
			game::gpGame->PollNetworkClient();
			game::gpGame->Reconcile();
		}

		try
		{
			pGame->UpdateFramesAndRender(menuInput, bLostFocus, bUpdateFrames);
		}
		catch (DeviceLostException& rDeviceLostException)
		{
			Log("Caught rDeviceLostException: {}", rDeviceLostException.what());
			pGraphics.reset();
			pGraphics = std::make_unique<Graphics>(hinstance, sHwnd);
			// Prevent time jumps after device recreation
			ResetRealTime();
		}

		{
			// Heap: ENet packet assembly for input
			ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
			game::gpGame->SendNetworkInput();
		}

		// Audio update
		gpProfileManager->CpuStart(kCpuTimerAudio);
		gpAudioManager->Update(pGame->CurrentFrame(game::gpGame->mHumanGridCoord));
		gpProfileManager->CpuStop(kCpuTimerAudio, false);

		sbUseCrosshair = pGame->ShouldUseCrosshair();
#else
		// Server: poll network and process inputs before frame update
		{
			// Heap: ENet polling and game server methods allocate vectors for inputs, spawns, and status changes
			ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
			gpNetworkServer->Poll();
			game::gpGame->HandleNewClientsServer();
			game::gpGame->ProcessSpawnRequestsServer();
		}

		pGame->UpdateFramesOnly(menuInput, bLostFocus, bUpdateFrames);

		// Server: finalize spawns, update subscriptions, and broadcast
		{
			// Heap: SendFullState, BroadcastUpdate, and UpdateClientSubscription allocate for serialization and compression
			ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
			int64_t iFrame = pGame->FrameCounter();
			game::gpGame->FinalizeNewClientsServer(iFrame);
			game::gpGame->HandleSubscriptionUpdatesServer(iFrame);
			game::gpGame->BroadcastStatusChangesServer(iFrame);
		}

		// Repaint server display every tick
		{
			// Heap: Win32 InvalidateRect may trigger internal GDI allocations
			ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
			InvalidateRect(sHwnd, nullptr, FALSE);
		}

		ProcessMessages(true);
#endif
	}
	Log("Exit main loop\n\n");

	EnableAllocationTracking(false);

#ifdef BT_CLIENT
	// Wait for async render to complete before shutdown
	gpGraphics->WaitForRender();
#endif

#ifdef BT_CLIENT
	// Save settings
	game::Game::SaveSoundSettings();
#endif

	PostQuitMessage(0);
	ProcessMessages(true);
}

#ifdef BT_CLIENT
static int64_t siMonitorCount = 0;
static bool sbUseCurrentRect = false;
static RECT sWindowRect {};

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

bool ProcessMessages(bool bIgnoreFocus)
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

	if (bIgnoreFocus)
	{
		return false;
	}

	// While we don't have focus, process messages with GetMessage() which blocks until a message is available
	bool bLostFocus = !sbHasFocus;
	while (!sbHasFocus && !sbQuit)
	{
		GetMessage(&msg, nullptr, 0, 0);
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return bLostFocus;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#ifdef BT_CLIENT
	// Game handles cursor when ImGui doesn't want the mouse
	if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT && !ImGui::GetIO().WantCaptureMouse)
	{
		SetCursor(sbUseCrosshair ? sHcursorCrosshair : sHcursorArrow);
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
#endif

	switch (message)
	{
#ifdef BT_SERVER
		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
		{
			// Heap: GDI painting creates/destroys kernel objects that may trigger CRT allocations
			ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
			PaintServerDisplay(hWnd);
			return 0;
		}
#endif

		case WM_SETFOCUS:
		{
			Log("WM_SETFOCUS");

			if (!sbHasFocus)
			{
				sbHasFocus = true;

#ifdef BT_CLIENT
				SetCursor(sbUseCrosshair ? sHcursorCrosshair : sHcursorArrow);

				if (gpAudioManager->mpAudioEngine != nullptr)
				{
					gpAudioManager->mpAudioEngine->Resume();
				}

				gpRawInputManager->UpdateFocus(true, sHwnd);
#endif
			}

			break;
		}

		case WM_KILLFOCUS:
		{
			Log("WM_KILLFOCUS");

			if (sbHasFocus)
			{
				sbHasFocus = false;

#ifdef BT_CLIENT
				if (gpAudioManager->mpAudioEngine != nullptr)
				{
					gpAudioManager->mpAudioEngine->Suspend();
				}

				gpRawInputManager->UpdateFocus(false, sHwnd);
#endif
			}

			break;
		}

		case WM_INPUT:
		{
#ifdef BT_CLIENT
			gpRawInputManager->HandleRawInput(lParam);
#endif
			return 0;
		}

		case WM_SIZE:
		{
#ifdef BT_CLIENT
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

void HandleException(std::optional<const std::exception*> pException = std::nullopt)
{
	int iResult = MessageBox(nullptr, "Save crash report to desktop?", game::kGameName.data(), MB_YESNO | MB_SYSTEMMODAL);

	std::wstring gameName = common::ToWstring(game::kGameName);

	static wchar_t spcPath[MAX_PATH + 1] {};
	if (iResult == IDYES)
	{
		SHGetSpecialFolderPathW(HWND_DESKTOP, spcPath, CSIDL_DESKTOP, FALSE);
	}
	else
	{
		PWSTR pWideChar = nullptr;
		SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &pWideChar);
		wcscpy_s(spcPath, std::size(spcPath), pWideChar);
		CoTaskMemFree(pWideChar);

		wcscat_s(spcPath, std::size(spcPath), L"\\");
		wcscat_s(spcPath, std::size(spcPath), gameName.c_str());

		std::filesystem::create_directories(spcPath);
	}

	std::wstring crashReportPath(L"\\");
	crashReportPath.append(gameName);
	std::replace(crashReportPath.begin(), crashReportPath.end(), ' ', '-');
	crashReportPath.append(L"-Crash-Report.txt");
	wcscat_s(spcPath, std::size(spcPath), crashReportPath.c_str());

	std::ofstream ofstream(spcPath);

	ofstream << "\n\n\nPlease send this crash report to brokenteapotstudios@gmail.com, and if possible describe exactly what you were doing when it occurred.\n" << std::flush;
	ofstream << "Game name: " << game::kGameName << "\n" << std::flush;
	ofstream << "Game version: " << game::kiGameVersion << "\n" << std::flush;

	if (pException.has_value())
	{
		ofstream << "\n\n\n" << pException.value()->what() << "\n" << std::flush;
	}
	else
	{
		ofstream << "\n\n\nUnknown exception\n" << std::flush;
	}

	ofstream << "\n\n\n<Begin callstack>\n" << std::flush;
	common::OfstreamStackWalker ofstreamStackWalker(StackWalker::AfterCatch, &ofstream);
	ofstreamStackWalker.ShowCallstack();
	ofstream << "<End callstack>\n" << std::flush;

	ofstream << "\n\n\n<Begin DxDiag>\n" << std::flush;
	ofstream << sDxDiag;
	ofstream << "<End DxDiag>\n" << std::flush;

	ofstream << "\n\n\n<Begin Log>\n" << std::flush;
	gpFileManager->WriteLogFile(ofstream);
	ofstream << "<End Log>\n" << std::flush;

	ofstream.close();
}

void ReadDxDiag()
{
	if (IsDebuggerPresent())
	{
		return;
	}

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	common::ThreadLocal threadLocal(1024, common::kThreadDxDiag);

	try
	{
		CHECK_HRESULT(CoInitialize(nullptr));

		Microsoft::WRL::ComPtr<IDxDiagProvider> pIdxDiagProvider;
		CHECK_HRESULT(CoCreateInstance(CLSID_DxDiagProvider, nullptr, CLSCTX_INPROC_SERVER, IID_IDxDiagProvider, (LPVOID*)&pIdxDiagProvider));

		DXDIAG_INIT_PARAMS dxdiagInitParams {.dwSize = sizeof(DXDIAG_INIT_PARAMS), .dwDxDiagHeaderVersion = DXDIAG_DX9_SDK_VERSION, .bAllowWHQLChecks = false, .pReserved = nullptr};
		CHECK_HRESULT(pIdxDiagProvider->Initialize(&dxdiagInitParams));

		Microsoft::WRL::ComPtr<IDxDiagContainer> pRoot;
		CHECK_HRESULT(pIdxDiagProvider->GetRootContainer(&pRoot));

		Microsoft::WRL::ComPtr<IDxDiagContainer> pDisplayDevices;
		CHECK_HRESULT(pRoot->GetChildContainer(L"DxDiag_DisplayDevices", &pDisplayDevices));

		DWORD uiChildCount = 0;
		CHECK_HRESULT(pDisplayDevices->GetNumberOfChildContainers(&uiChildCount));
		Log("DxDiag found {} children", uiChildCount);
		for (DWORD i = 0; i < uiChildCount; ++i)
		{
			WCHAR pcChildName[256] {};
			CHECK_HRESULT(pDisplayDevices->EnumChildContainerNames(i, pcChildName, 256));
			Microsoft::WRL::ComPtr<IDxDiagContainer> pChild;
			CHECK_HRESULT(pDisplayDevices->GetChildContainer(pcChildName, &pChild));

			DWORD uiPropCount = 0;
			pChild->GetNumberOfProps(&uiPropCount);
			Log("    {} props", uiPropCount);
			for (DWORD j = 0; j < uiPropCount; ++j)
			{
				WCHAR pcPropName[256] {};
				pChild->EnumPropNames(j, pcPropName, static_cast<DWORD>(std::size(pcPropName) - 1));

				VARIANT variant {};
				pChild->GetProp(pcPropName, &variant);
				if (variant.vt == VT_BSTR)
				{
					sDxDiag += common::ToString(pcPropName);
					sDxDiag += ": ";
					sDxDiag += common::ToString(variant.bstrVal);
					sDxDiag += "\n";
				}
			}
		}
	}
	catch ([[maybe_unused]] const std::exception& rException)
	{
		Log("Failed to read DxDiag: {}", rException.what());
	}
	catch (...)
	{
		Log("Failed to read DxDiag");
	}
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

#ifdef BT_CLIENT
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
