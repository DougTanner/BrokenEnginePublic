#include "Audio/AudioManager.h"
#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Input/RawInputManager.h"
#include "Profile/ProfileManager.h"

#include "Game.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace engine
{

static bool sbQuit = false;

static HCURSOR sHcursorArrow = nullptr;
static HCURSOR sHcursorCrosshair = nullptr;
static bool sbUseCrosshair = false;

static HWND sHwnd = nullptr;
static HMONITOR sHmonitor = nullptr;
static MONITORINFO sMonitorInfo {};

static bool sbHasFocus = false;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

void FindMonitor(bool bUseCurrentRect);
VkExtent2D SetupWindow(bool bFullscreen, LONG& riWindowStyle, RECT& rWindowRect);
bool ProcessMessages(bool bIgnoreFocus = false);

static std::string sDxDiag;
void ReadDxDiag();

void MainThread(HINSTANCE hinstance)
{
	common::ThreadLocal threadLocal(10 * 1024 * 1024);

#if defined(ENABLE_PROFILING)
	auto pProfileManager = std::make_unique<ProfileManager>();
#endif

	// Save one core for the main thread
	giBackgroundThreadCount = std::max(1ll, common::HardwareCoreCount() - 1);

	if (!XMVerifyCPUSupport()) [[unlikely]]
	{
		throw std::runtime_error("Your CPU does not support SSE4.1 instructions");
	}

	if (SetProcessDPIAware() == 0) [[unlikely]]
	{
		throw std::runtime_error("SetProcessDPIAware failed");
	}

	// An optimization to consider is to disable the handling of denormals for the vector operations used by DirectXMath
	// https://docs.microsoft.com/en-us/windows/win32/dxmath/pg-xnamath-optimizing#denormals
	unsigned int uiCurrentState = 0;
	_controlfp_s(&uiCurrentState, _DN_FLUSH, _MCW_DN);

#if defined(ENABLE_DXDIAG)
	// DxDiag
	std::future<void> readDxDiag;
	if (!IsDebuggerPresent()) [[likely]]
	{
		readDxDiag = std::async(std::launch::async, ReadDxDiag);
	}
#endif

	// Audio
	auto pAudioManager = std::make_unique<AudioManager>();

	LOG("\nGame name: {}", game::kGameName);
	LOG("Game version: {}", game::kiGameVersion);
	LOG("Compiled with Windows 10 SDK version: {}.{}", VER_PRODUCTBUILD, VER_PRODUCTBUILD_QFE);
	LOG("Compiled with Vulkan SDK version: {}\n", VK_HEADER_VERSION);
	static_assert(VK_HEADER_VERSION >= 304, "Update the Vulkan SDK");

	// Cursor
	sHcursorArrow = LoadCursor(nullptr, IDC_ARROW);
	sHcursorCrosshair = LoadCursor(nullptr, IDC_CROSS);
	common::ScopedLambda destroyCursor([]()
	{
		DestroyCursor(sHcursorArrow);
		DestroyCursor(sHcursorCrosshair);
	});

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
		LOG("Unregister class");
		UnregisterClass(game::kGameName.data(), hinstance);
	});

	// Setup window rect & matrices
	LONG iWindowStyle = 0;
	RECT windowRect {};
	gWantedFramebufferExtent2D = SetupWindow(gFullscreen.Get<bool>(), iWindowStyle, windowRect);

	if (gWantedFramebufferExtent2D.height < 2160)
	{
		gSampleCount.Set<VkSampleCountFlagBits>(VK_SAMPLE_COUNT_4_BIT);
	}

	// Create window
	auto pRawInputManager = std::make_unique<RawInputManager>();
	sHwnd = CreateWindow(game::kGameName.data(), game::kGameName.data(), iWindowStyle, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, nullptr, nullptr, hinstance, nullptr);
	pRawInputManager->mHwnd = sHwnd;
	if (sHwnd == nullptr)
	{
		throw std::runtime_error("CreateWindow failed");
	}
	common::ScopedLambda destroyWindow([]()
	{
		ProcessMessages(true);

		if (sHwnd != nullptr)
		{
			LOG("Destroy window");
			DestroyWindow(sHwnd);
			sHwnd = nullptr;
		}
	});

	// Load settings
	game::Game::LoadSoundSettings();

	// Initialize graphics
	BOOT_TIMER_START(kBootTimerVulkan);
	auto pGraphics = std::make_unique<Graphics>(hinstance, sHwnd);

	// Wait for islands to load and initialize heightmaps
	BOOT_TIMER_START(kBootTimerWaitForIslands);
	gpIslands->WaitForElevationMaps();
	BOOT_TIMER_STOP(kBootTimerWaitForIslands);

	// Load game
	auto pCamera = std::make_unique<game::Camera>();
	auto pGame = std::make_unique<game::Game>();

	// Input
	auto pInput = std::make_unique<game::Input>();
	game::gpInput = pInput.get();

	BOOT_TIMER_STOP(kBootTimerVulkan);

	// Ensure priority textures are ready
	BOOT_TIMER_START(kBootTimerWaitForPriorityTextures);
	gpFileManager->WaitForChunks(TextureManager::smPriorityTextures);
	BOOT_TIMER_STOP(kBootTimerWaitForPriorityTextures);

	// Render and present all framebuffers, then show window
	BOOT_TIMER_START(kBootTimerRenderPresent);
	for (int64_t i = 0; i < static_cast<int64_t>(gpCommandBufferManager->mPerFramebufferCommandBuffers.size()); ++i)
	{
		// DT: TODO Does this render a black frame?
		ResetRealTime();
		gpGraphics->RenderPresentAcquire(pGame->CurrentFrame());
	}
	BOOT_TIMER_STOP(kBootTimerRenderPresent);

	ShowWindow(sHwnd, SW_SHOWDEFAULT);
	common::ScopedLambda hideWindow([]()
	{
		LOG("Hide window");
		ShowWindow(sHwnd, SW_HIDE);
	});
	BringWindowToTop(sHwnd);
	SetFocus(sHwnd);
	ProcessMessages(true);

	BOOT_TIMERS_LOG();

	SCOPED_LOG_INDENT();
	LOG("\nEnter main loop");
	ResetRealTime();

	while (true)
	{
		CPU_PROFILE_START(kCpuTimerMessagesAndInput);

		// Handle fullscreen toggle
		bool bWantedFullscreen = gFullscreen.Get<bool>();
		bool bIsFullscreen = (iWindowStyle & WS_POPUP) != 0;
		if (bIsFullscreen != bWantedFullscreen)
		{
			SetupWindow(bWantedFullscreen, iWindowStyle, windowRect);
			SetWindowLongPtr(sHwnd, GWL_STYLE, iWindowStyle);
			SetWindowPos(sHwnd, nullptr, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, 0);
		}

		// Process Windows messages
		bool bLostFocus = ProcessMessages();
		if (sbQuit) [[unlikely]]
		{
			break;
		}

		// Input
		pRawInputManager->Update(bLostFocus);
		if (pInput->UpdateMenuInput(pRawInputManager->mRawInput)) [[unlikely]]
		{
			break;
		}
		gpUiManager->Update(pInput->GetMenuInput());

		bool bUpdateFrames = pGame->PreUpdate(pInput->GetMenuInput(), bLostFocus);
		if (pGame->mGameFlags & engine::GameFlags::kQuit) [[unlikely]]
		{
			break;
		}

		CPU_PROFILE_STOP(kCpuTimerMessagesAndInput);

		try
		{
			pGame->UpdateFramesAndRender(pInput->GetMenuInput(), bLostFocus, bUpdateFrames);
		}
		catch (DeviceLostException& rDeviceLostException)
		{
			LOG("Caught rDeviceLostException: {}", rDeviceLostException.what());
			pGraphics.reset();
			pGraphics = std::make_unique<Graphics>(hinstance, sHwnd);
		}

		// Audio update
		CPU_PROFILE_START(kCpuTimerAudio);
		gpAudioManager->Update(pGame->CurrentFrame());
		CPU_PROFILE_STOP(kCpuTimerAudio);

		// Update cursor visual
		// DT: GAMELOGIC
		sbUseCrosshair = pGame->CurrentFrame().interpolate.flags & game::FrameFlags::kGame && pGame->meUiState == game::UiState::kNone;
	}
	LOG("Exit main loop\n\n");

	// Save settings
	pGame->WriteAutosave();
	game::Game::SaveSoundSettings();

	PostQuitMessage(0);
	ProcessMessages(true);
}

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
		LOG("Window left top: {}, {}", sWindowRect.left, sWindowRect.top);
	}

	LOG("Monitors:");
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
		LOG("  {}: {} x {}{}{}", siMonitorCount++, iWidth, iHeight, bPrimary ? " (Primary)" : "", bRectIsInMonitor ? " (Monitor)" : "");

		if (sHmonitor == nullptr || (sbUseCurrentRect && bRectIsInMonitor) || (!sbUseCurrentRect && bPrimary))
		{
			sHmonitor = hmonitor;
			sMonitorInfo = monitorinfo;
		}

		return TRUE;
	}, 0);
	LOG("");
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
	#if defined(BT_DEBUG)
		rWindowRect.right = rWindowRect.left + 1920;
		rWindowRect.bottom = rWindowRect.top + 1080;
	#endif
	}

	LONG iFramebufferWidth = rWindowRect.right - rWindowRect.left;
	LONG iFramebufferHeight = rWindowRect.bottom - rWindowRect.top;
	LOG("Set {} window {} x {} at ({}, {})", (riWindowStyle & WS_OVERLAPPEDWINDOW) != 0 ? "WS_OVERLAPPEDWINDOW" : "WS_POPUP", iFramebufferWidth, iFramebufferHeight, rWindowRect.left, rWindowRect.top);

	if ((riWindowStyle & WS_OVERLAPPEDWINDOW) != 0)
	{
		AdjustWindowRect(&rWindowRect, riWindowStyle, FALSE);
	}

	return {static_cast<uint32_t>(iFramebufferWidth), static_cast<uint32_t>(iFramebufferHeight)};
}

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

	switch (message)
	{
		case WM_SETCURSOR:
			SetCursor(sbUseCrosshair ? sHcursorCrosshair : sHcursorArrow);
			break;

		case WM_SETFOCUS:
		{
			LOG("WM_SETFOCUS");

			if (!sbHasFocus)
			{
				sbHasFocus = true;

				SetCursor(sbUseCrosshair ? sHcursorCrosshair : sHcursorArrow);

				if (gpAudioManager->mpAudioEngine != nullptr)
				{
					gpAudioManager->mpAudioEngine->Resume();
				}

				gpRawInputManager->UpdateFocus(true, sHwnd);
			}

			break;
		}

		case WM_KILLFOCUS:
		{
			LOG("WM_KILLFOCUS");

			if (sbHasFocus)
			{
				sbHasFocus = false;

				if (gpAudioManager->mpAudioEngine != nullptr)
				{
					gpAudioManager->mpAudioEngine->Suspend();
				}

				gpRawInputManager->UpdateFocus(false, sHwnd);
			}

			break;
		}

		case WM_INPUT:
		{
			gpRawInputManager->HandleRawInput(lParam);
			return 0;
		}

		case WM_SIZE:
		{
			gWantedFramebufferExtent2D = {static_cast<uint32_t>(lParam) & 0xFFFF, static_cast<uint32_t>(lParam) >> 16};
			LOG("WM_SIZE: {} x {}", gWantedFramebufferExtent2D.width, gWantedFramebufferExtent2D.height);
			break;
		}

		case WM_CLOSE:
		case WM_QUIT:
		{
			LOG("WM_CLOSE or WM_QUIT");
			sbQuit = true;
			return 0;
		}

		case WM_DESTROY:
		{
			LOG("WM_DESTROY");
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

		DXDIAG_INIT_PARAMS dxdiagInitParams {.dwSize = sizeof(DXDIAG_INIT_PARAMS), .dwDxDiagHeaderVersion = DXDIAG_DX9_SDK_VERSION, .bAllowWHQLChecks = false, .pReserved = NULL};
		CHECK_HRESULT(pIdxDiagProvider->Initialize(&dxdiagInitParams));

		Microsoft::WRL::ComPtr<IDxDiagContainer> pRoot;
		CHECK_HRESULT(pIdxDiagProvider->GetRootContainer(&pRoot));

		Microsoft::WRL::ComPtr<IDxDiagContainer> pDisplayDevices;
		CHECK_HRESULT(pRoot->GetChildContainer(L"DxDiag_DisplayDevices", &pDisplayDevices));

		DWORD uiChildCount = 0;
		CHECK_HRESULT(pDisplayDevices->GetNumberOfChildContainers(&uiChildCount));
		LOG("DxDiag found {} children", uiChildCount);
		for (DWORD i = 0; i < uiChildCount; ++i)
		{
			WCHAR pcChildName[256] {};
			CHECK_HRESULT(pDisplayDevices->EnumChildContainerNames(i, pcChildName, 256));
			Microsoft::WRL::ComPtr<IDxDiagContainer> pChild;
			CHECK_HRESULT(pDisplayDevices->GetChildContainer(pcChildName, &pChild));

			DWORD uiPropCount = 0;
			pChild->GetNumberOfProps(&uiPropCount);
			LOG("    {} props", uiPropCount);
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
		LOG("Failed to read DxDiag: {}", rException.what());
	}
	catch (...)
	{
		LOG("Failed to read DxDiag");
	}
}

} // namespace engine

#if defined(_CRTDBG_MAP_ALLOC)

#pragma warning(disable:4074)
#pragma init_seg(compiler)

struct CrtBreakAllocSetter
{
	CrtBreakAllocSetter()
	{
		// _crtBreakAlloc = 41353;
	}
};

CrtBreakAllocSetter gCrtBreakAllocSetter; 

_Ret_notnull_ _Post_writable_byte_size_(_Size) _VCRT_ALLOCATOR void* __CRTDECL operator new(size_t _Size)
{
	return malloc(_Size);
}

void __CRTDECL operator delete(void* _Block)
{
	return free(_Block);
}

_Ret_notnull_ _Post_writable_byte_size_(_Size) _VCRT_ALLOCATOR void* __CRTDECL operator new[](size_t _Size)
{
	return malloc(_Size);
}

void __CRTDECL operator delete[](void* _Block)
{
	return free(_Block);
}

int MallocHook([[maybe_unused]] int allocType, [[maybe_unused]] void* userData, [[maybe_unused]] size_t size, [[maybe_unused]] int blockType, [[maybe_unused]] long requestNumber, [[maybe_unused]] const unsigned char* filename, [[maybe_unused]] int lineNumber)
{
	return TRUE;
}

#endif

int WINAPI wWinMain(_In_ HINSTANCE hInstance, [[maybe_unused]] _In_opt_ HINSTANCE hPrevInstance, [[maybe_unused]] _In_ LPWSTR lpCmdLine, [[maybe_unused]] _In_ int nShowCmd)
{
#if defined(_CRTDBG_MAP_ALLOC)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	_CrtSetAllocHook(&MallocHook);
#endif

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	// Windows::Foundation::Initialize is required for XAudio2 (and possibly gampads as well)
	HRESULT hresult = Windows::Foundation::Initialize(RO_INIT_MULTITHREADED);
	if (hresult != S_OK) [[unlikely]]
	{
		MessageBox(nullptr, common::HresultToString(hresult).data(), "Windows::Foundation::Initialize", MB_OK | MB_SYSTEMMODAL);
		return 0;
	}

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

	LOG("Windows foundation uninitialize");
	Windows::Foundation::Uninitialize();

	return 0;
}
