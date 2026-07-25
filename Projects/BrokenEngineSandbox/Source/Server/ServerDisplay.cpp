#include "Pch.h"

#if defined(BT_SERVER)

#include "Server/ServerDisplay.h"

#include "Network/Server/Server.h"

#include "Game.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Frame/Collections/Targets/Targets.h"
#include "Profile/ProfileManager.h"

namespace game
{

enum class ServerTab : uint8_t { kMap, kProfile };
static ServerTab seActiveTab = ServerTab::kMap;

static constexpr int64_t kiTabBarLeft = 250;
static constexpr int64_t kiTabHeight = 32;
static constexpr int64_t kiTabWidth = 100;
static constexpr int64_t kiTabCount = 2;
static constexpr std::string_view kTabNames[] = {"Map", "Profile"};

static constexpr int64_t kiCopyButtonWidth = 80;
static constexpr int64_t kiCopyButtonHeight = 28;
static constexpr int64_t kiCopyButtonMargin = 10;
static RECT sCopyButtonRect {};
static std::string sProfileText;

// Dirty-check state for the throttled repaint: coarse hash of the last content the window drew.
static uint64_t suLastContentHash = 0;
static bool sbHasLastContentHash = false;

// Persistent GDI objects (one window per process): the back-buffer DC/bitmap and font are created on first
// paint and the bitmap is recreated only when the client size changes; the palette brushes/pens are a fixed
// RGB set created once instead of per-cell per-paint. Nothing is explicitly destroyed — process teardown
// reclaims GDI objects.
static HDC shdcBuffer = nullptr;
static HBITMAP shbmBuffer = nullptr;
static int siBufferWidth = 0;
static int siBufferHeight = 0;
static HFONT shFont = nullptr;

static HBRUSH shBrushBackground = nullptr;  // RGB(30, 30, 30) — window / content background
static HBRUSH shBrushCellClient = nullptr;  // RGB(40, 60, 140) — client-authorized cell
static HBRUSH shBrushCellActive = nullptr;  // RGB(30, 100, 30) — active cell
static HBRUSH shBrushGrayFill = nullptr;    // RGB(50, 50, 50) — idle cell / copy button
static HBRUSH shBrushTabActive = nullptr;   // RGB(60, 60, 60) — active tab
static HBRUSH shBrushTabInactive = nullptr; // RGB(40, 40, 40) — inactive tab

static HPEN shPenCellBorderClient = nullptr; // RGB(100, 140, 255) — client cell border
static HPEN shPenGray = nullptr;             // RGB(80, 80, 80) — normal borders / tab bottom line
static HPEN shPenRed = nullptr;              // RGB(220, 40, 40) — subscribed-cell interior border
static HPEN shPenBlue = nullptr;             // RGB(100, 180, 255) — active tab border / copy button border

static void EnsureCachedGdiObjects()
{
	if (shFont != nullptr)
	{
		return;
	}

	shFont = CreateFont(28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

	shBrushBackground = CreateSolidBrush(RGB(30, 30, 30));
	shBrushCellClient = CreateSolidBrush(RGB(40, 60, 140));
	shBrushCellActive = CreateSolidBrush(RGB(30, 100, 30));
	shBrushGrayFill = CreateSolidBrush(RGB(50, 50, 50));
	shBrushTabActive = CreateSolidBrush(RGB(60, 60, 60));
	shBrushTabInactive = CreateSolidBrush(RGB(40, 40, 40));

	shPenCellBorderClient = CreatePen(PS_SOLID, 1, RGB(100, 140, 255));
	shPenGray = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
	shPenRed = CreatePen(PS_SOLID, 1, RGB(220, 40, 40));
	shPenBlue = CreatePen(PS_SOLID, 1, RGB(100, 180, 255));
}

void ServerUpdateDisplayStats()
{
	if constexpr (!kbProfiling)
	{
		return;
	}

	int64_t iTotalPlayers = 0;
	int64_t iTotalSpaceships = 0;
	int64_t iTotalBlasters = 0;
	int64_t iTotalMissiles = 0;
	int64_t iTotalTargets = 0;
	int64_t iTotalExplosions = 0;

	for (const engine::GridCoord& rCoord : game::gpGame->mActiveCoords)
	{
		const game::Frame& rFrame = game::gpGame->CurrentFrame(rCoord);
		iTotalPlayers += rFrame.interpolate.pPlayers->iCount;
		iTotalSpaceships += rFrame.interpolate.pSpaceships->iCount;
		iTotalBlasters += rFrame.interpolate.pBlasters->iCount;
		iTotalMissiles += rFrame.interpolate.pMissiles->iCount;
		iTotalTargets += rFrame.interpolate.pTargets->iCount;
		iTotalExplosions += rFrame.interpolate.explosions.iCount;
	}

	gpProfileManager->SetCount(game::kCpuCounterPlayers, iTotalPlayers);
	gpProfileManager->SetCount(game::kCpuCounterSpaceships, iTotalSpaceships);
	gpProfileManager->SetCount(game::kCpuCounterBlasters, iTotalBlasters);
	gpProfileManager->SetCount(game::kCpuCounterMissiles, iTotalMissiles);
	gpProfileManager->SetCount(game::kCpuCounterTargets, iTotalTargets);
	gpProfileManager->SetCount(engine::kCpuCounterExplosions, iTotalExplosions);

#if !defined(ENABLE_CRT_DEBUG_HEAP)
	mi_stats_merge();
	mi_stats_t miStats = {};
	miStats.size = sizeof(mi_stats_t);
	miStats.version = MI_STAT_VERSION;
	mi_stats_get(&miStats);

	gpProfileManager->miMimallocCommittedMib = miStats.committed.current / (1024 * 1024);
	gpProfileManager->miMimallocPeakCommittedMib = miStats.committed.peak / (1024 * 1024);
	gpProfileManager->miMimallocHeapUsedMib = miStats.page_committed.current / (1024 * 1024);
	gpProfileManager->miMimallocPeakHeapUsedMib = miStats.page_committed.peak / (1024 * 1024);
#endif

	gpProfileManager->UpdateProfileText();
}

bool ServerDisplayContentChanged()
{
	// Coarse FNV-1a fold over the substantive stats/map inputs the window draws — active coords and their
	// entity totals, the client list with its authorized/subscribed coords, the active tab, and memory
	// stats. The free-running tick/time and CPU-timer values are deliberately excluded so an idle server
	// (stable entity/map state) stops repainting instead of blitting an all-but-identical frame at 4 Hz.
	// POD-only fold — no heap allocation, so it stays inside the caller's ScopedSuppressAllocationTracking.
	uint64_t uHash = 14695981039346656037ULL;
	auto Mix = [&uHash](int64_t iValue)
	{
		uHash ^= static_cast<uint64_t>(iValue);
		uHash *= 1099511628211ULL;
	};

	Mix(static_cast<int64_t>(seActiveTab));

	for (const engine::GridCoord& rCoord : game::gpGame->mActiveCoords)
	{
		Mix(rCoord.x);
		Mix(rCoord.y);

		// Mix each count separately — summing would alias conversions (e.g. spaceship death -1 spaceship
		// +1 explosion leaves the sum unchanged) and skip a repaint whose displayed numbers did change.
		const game::Frame& rFrame = game::gpGame->CurrentFrame(rCoord);
		Mix(rFrame.interpolate.pPlayers->iCount);
		Mix(rFrame.interpolate.pSpaceships->iCount);
		Mix(rFrame.interpolate.pBlasters->iCount);
		Mix(rFrame.interpolate.pMissiles->iCount);
		Mix(rFrame.interpolate.pTargets->iCount);
		Mix(rFrame.interpolate.explosions.iCount);
	}

	const std::vector<engine::ClientConnection>& rClients = engine::gpServer->mClients;
	Mix(static_cast<int64_t>(rClients.size()));
	for (const engine::ClientConnection& rClient : rClients)
	{
		for (const engine::GridCoord& rOwnedCoord : rClient.authorizedCoords)
		{
			Mix(rOwnedCoord.x);
			Mix(rOwnedCoord.y);
		}

		for (int64_t i = 0; i < std::ssize(rClient.slots); ++i)
		{
			if (rClient.slots.at(i).subscription.flags & engine::SubscriptionFlags::kActive)
			{
				Mix(rClient.slots.at(i).subscription.coord.x);
				Mix(rClient.slots.at(i).subscription.coord.y);
			}
		}
	}

#if !defined(ENABLE_CRT_DEBUG_HEAP)
	Mix(gpProfileManager->miMimallocCommittedMib);
	Mix(gpProfileManager->miMimallocHeapUsedMib);
#endif

	if (sbHasLastContentHash && uHash == suLastContentHash)
	{
		return false;
	}

	suLastContentHash = uHash;
	sbHasLastContentHash = true;
	return true;
}

static void PaintGridMap(HDC hdcBuffer, char* pcLine, size_t iLineSize, int iMapLeft, int iMapTop, int iMapWidth, int iMapHeight, const std::vector<engine::ClientConnection>& rClients)
{
	if (game::gpGame->mActiveCoords.empty())
	{
		return;
	}

	// Compute grid bounds with 1-cell padding
	int32_t iMinX = game::gpGame->mActiveCoords.at(0).x;
	int32_t iMaxX = iMinX;
	int32_t iMinY = game::gpGame->mActiveCoords.at(0).y;
	int32_t iMaxY = iMinY;

	for (const engine::GridCoord& rCoord : game::gpGame->mActiveCoords)
	{
		iMinX = std::min(iMinX, rCoord.x);
		iMaxX = std::max(iMaxX, rCoord.x);
		iMinY = std::min(iMinY, rCoord.y);
		iMaxY = std::max(iMaxY, rCoord.y);
	}

	iMinX -= 1;
	iMaxX += 1;
	iMinY -= 1;
	iMaxY += 1;

	int32_t iGridWidth = iMaxX - iMinX + 1;
	int32_t iGridHeight = iMaxY - iMinY + 1;

	int iCellWidth = std::min(60, iMapWidth / iGridWidth);
	int iCellHeight = std::min(60, iMapHeight / iGridHeight);
	int iCellSize = std::min(iCellWidth, iCellHeight);

	int iGridPixelWidth = iGridWidth * iCellSize;
	int iGridPixelHeight = iGridHeight * iCellSize;
	int iOffsetX = (iMapWidth - iGridPixelWidth) / 2;
	int iOffsetY = (iMapHeight - iGridPixelHeight) / 2;

	if (iCellSize < 4)
	{
		return;
	}

	for (int32_t gy = iMinY; gy <= iMaxY; ++gy)
	{
		for (int32_t gx = iMinX; gx <= iMaxX; ++gx)
		{
			int iCellLeft = iMapLeft + iOffsetX + (gx - iMinX) * iCellSize;
			int iCellTop = iMapTop + iOffsetY + (iMaxY - gy) * iCellSize;
			RECT cellRect {iCellLeft, iCellTop, iCellLeft + iCellSize, iCellTop + iCellSize};

			engine::GridCoord coord {gx, gy};
			bool bIsActive = false;
			bool bIsSubscribed = false;
			int64_t iClientsInCell = 0;

			for (const engine::GridCoord& rActiveCoord : game::gpGame->mActiveCoords)
			{
				if (rActiveCoord == coord)
				{
					bIsActive = true;
					break;
				}
			}

			for (const engine::ClientConnection& rClient : rClients)
			{
				for (const engine::GridCoord& rOwnedCoord : rClient.authorizedCoords)
				{
					if (rOwnedCoord == coord)
					{
						++iClientsInCell;
					}
				}

				if (!bIsSubscribed && rClient.FindSlotForCoord(coord) >= 0)
				{
					bIsSubscribed = true;
				}
			}

			// Fill cell
			HBRUSH hBrushCell = shBrushGrayFill;
			if (iClientsInCell > 0)
			{
				hBrushCell = shBrushCellClient;
			}
			else if (bIsActive)
			{
				hBrushCell = shBrushCellActive;
			}
			FillRect(hdcBuffer, &cellRect, hBrushCell);

			// Border
			HPEN hPen = (iClientsInCell > 0) ? shPenCellBorderClient : shPenGray;
			HPEN hOldPen = static_cast<HPEN>(SelectObject(hdcBuffer, hPen));
			MoveToEx(hdcBuffer, iCellLeft, iCellTop, nullptr);
			LineTo(hdcBuffer, iCellLeft + iCellSize, iCellTop);
			LineTo(hdcBuffer, iCellLeft + iCellSize, iCellTop + iCellSize);
			LineTo(hdcBuffer, iCellLeft, iCellTop + iCellSize);
			LineTo(hdcBuffer, iCellLeft, iCellTop);
			SelectObject(hdcBuffer, hOldPen);

			// Red interior border for subscribed cells
			if (bIsSubscribed && iCellSize >= 20)
			{
				static constexpr int kiInset = 2;
				HPEN hOldPen2 = static_cast<HPEN>(SelectObject(hdcBuffer, shPenRed));
				HBRUSH hOldBrush = static_cast<HBRUSH>(SelectObject(hdcBuffer, GetStockObject(NULL_BRUSH)));
				Rectangle(hdcBuffer, iCellLeft + kiInset, iCellTop + kiInset, iCellLeft + iCellSize - kiInset + 1, iCellTop + iCellSize - kiInset + 1);
				SelectObject(hdcBuffer, hOldBrush);
				SelectObject(hdcBuffer, hOldPen2);
			}

			// Cell labels for active cells
			if (bIsActive && iCellSize >= 24)
			{
				const game::Frame& rFrame = game::gpGame->CurrentFrame(coord);
				int64_t iEntityCount = rFrame.interpolate.pPlayers->iCount +
				                       rFrame.interpolate.pSpaceships->iCount +
				                       rFrame.interpolate.pBlasters->iCount +
				                       rFrame.interpolate.pMissiles->iCount +
				                       rFrame.interpolate.pTargets->iCount +
				                       rFrame.interpolate.explosions.iCount;

				SetTextColor(hdcBuffer, RGB(220, 220, 220));

				int iLineLength = std::min(std::snprintf(pcLine, iLineSize, "(%d,%d)", gx, gy), static_cast<int>(iLineSize) - 1);
				TextOutA(hdcBuffer, iCellLeft + 2, iCellTop + 2, pcLine, iLineLength);

				iLineLength = std::min(std::snprintf(pcLine, iLineSize, "%lld", iEntityCount), static_cast<int>(iLineSize) - 1);
				TextOutA(hdcBuffer, iCellLeft + 2, iCellTop + 28, pcLine, iLineLength);

				if (iClientsInCell > 0)
				{
					SetTextColor(hdcBuffer, RGB(150, 200, 255));
					iLineLength = std::min(std::snprintf(pcLine, iLineSize, "%lld", iClientsInCell), static_cast<int>(iLineSize) - 1);
					TextOutA(hdcBuffer, iCellLeft + iCellSize - 20, iCellTop + 2, pcLine, iLineLength);
				}
			}
		}
	}
}

static void PaintWorkbufferText(HDC hdcBuffer, std::string_view svText, int64_t iX, int64_t& riY, int64_t iLineHeight)
{
	size_t iStart = 0;
	while (iStart < svText.size())
	{
		size_t iEnd = svText.find('\n', iStart);
		if (iEnd == std::string_view::npos)
		{
			iEnd = svText.size();
		}

		std::string_view svLine = svText.substr(iStart, iEnd - iStart);
		if (!svLine.empty())
		{
			TextOutA(hdcBuffer, static_cast<int>(iX), static_cast<int>(riY), svLine.data(), static_cast<int>(svLine.size()));
		}
		riY += iLineHeight;
		iStart = iEnd + 1;
	}
}

static void CopyProfileToClipboard(HWND hWnd)
{
	if (sProfileText.empty())
	{
		return;
	}

	if (OpenClipboard(hWnd) == 0)
	{
		return;
	}

	EmptyClipboard();
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sProfileText.size() + 1);
	if (hMem != nullptr)
	{
		char* pDest = static_cast<char*>(GlobalLock(hMem));
		if (pDest != nullptr)
		{
			std::memcpy(pDest, sProfileText.data(), sProfileText.size());
			pDest[sProfileText.size()] = '\0';
			GlobalUnlock(hMem);
			SetClipboardData(CF_TEXT, hMem);
		}
		else
		{
			GlobalFree(hMem);
		}
	}
	CloseClipboard();
}

void HandleServerClick(HWND hWnd, int64_t iX, int64_t iY)
{
	// Tab bar
	if (iY <= kiTabHeight && iX >= kiTabBarLeft)
	{
		int64_t iTabIndex = (iX - kiTabBarLeft) / kiTabWidth;
		if (iTabIndex == 0)
		{
			seActiveTab = ServerTab::kMap;
		}
		else if (iTabIndex == 1)
		{
			seActiveTab = ServerTab::kProfile;
		}
		return;
	}

	// Copy button (Profile tab only)
	if (seActiveTab == ServerTab::kProfile)
	{
		POINT pt {static_cast<LONG>(iX), static_cast<LONG>(iY)};
		if (PtInRect(&sCopyButtonRect, pt) != 0)
		{
			CopyProfileToClipboard(hWnd);
		}
	}
}

static void PaintProfilePanel(HDC hdcBuffer, int iLeft, int iTop, [[maybe_unused]] int iWidth, int iHeight)
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	int64_t iTextX = iLeft + 10;
	int64_t iTextY = iTop;
	int64_t iLineHeight = 32;

	// Heap: std::string operations for clipboard cache
	ScopedSuppressAllocationTracking suppress;
	sProfileText.clear();

	// FPS header
	char pcLine[256] {};
	int iLineLength = std::min(std::snprintf(pcLine, sizeof(pcLine), "FPS: %lld  Potential: %lld",
		gpProfileManager->mFullUpdatesInTheLastSecond.Get(),
		gpProfileManager->GetCpuTimer(game::kCpuTimerFrameUpdate).smoothedMicroseconds.Average() > 0
			? 1'000'000 / gpProfileManager->GetCpuTimer(game::kCpuTimerFrameUpdate).smoothedMicroseconds.Average()
			: static_cast<int64_t>(0)), static_cast<int>(sizeof(pcLine)) - 1);
	SetTextColor(hdcBuffer, RGB(100, 180, 255));
	TextOutA(hdcBuffer, static_cast<int>(iTextX), static_cast<int>(iTextY), pcLine, iLineLength);
	sProfileText += pcLine;
	sProfileText += "\n";
	iTextY += iLineHeight + 4;

	// Re-evaluate row visibility on the shared 2s cadence (timers and counters together this paint).
	bool bReevaluate = gpProfileManager->TickVisibilityCadence();

	// CPU timers
	{
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
		engine::FormatCpuTimersText(rWorkbuffer, *gpProfileManager, bReevaluate);
		std::string_view svTimers = rWorkbuffer.View();
		SetTextColor(hdcBuffer, RGB(220, 220, 220));
		PaintWorkbufferText(hdcBuffer, svTimers, iTextX, iTextY, iLineHeight);
		sProfileText += svTimers;
	}

	iTextY += 4;

	// CPU counters
	{
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
		engine::FormatCpuCountersText(rWorkbuffer, *gpProfileManager, bReevaluate);
		std::string_view svCounters = rWorkbuffer.View();
		SetTextColor(hdcBuffer, RGB(150, 220, 150));
		PaintWorkbufferText(hdcBuffer, svCounters, iTextX, iTextY, iLineHeight);
		sProfileText += svCounters;
	}

#if !defined(ENABLE_CRT_DEBUG_HEAP)
	// Memory stats
	iTextY += 4;
	SetTextColor(hdcBuffer, RGB(220, 180, 100));

	auto memLine = [&](const char* pcFormat, int64_t iValue)
	{
		int iMemLineLength = std::min(std::snprintf(pcLine, sizeof(pcLine), pcFormat, iValue), static_cast<int>(sizeof(pcLine)) - 1);
		TextOutA(hdcBuffer, static_cast<int>(iTextX), static_cast<int>(iTextY), pcLine, iMemLineLength);
		sProfileText += pcLine;
		sProfileText += "\n";
		iTextY += iLineHeight;
	};

	memLine("Committed: %lld MiB", gpProfileManager->miMimallocCommittedMib);
	memLine("Peak cmtd: %lld MiB", gpProfileManager->miMimallocPeakCommittedMib);
	memLine("Heap used: %lld MiB", gpProfileManager->miMimallocHeapUsedMib);
	memLine("Peak heap: %lld MiB", gpProfileManager->miMimallocPeakHeapUsedMib);
#endif

	// Copy button
	int64_t iButtonLeft = iLeft + kiCopyButtonMargin;
	int64_t iButtonTop = iTop + iHeight - kiCopyButtonHeight - kiCopyButtonMargin;
	sCopyButtonRect = {static_cast<int>(iButtonLeft), static_cast<int>(iButtonTop), static_cast<int>(iButtonLeft + kiCopyButtonWidth), static_cast<int>(iButtonTop + kiCopyButtonHeight)};

	FillRect(hdcBuffer, &sCopyButtonRect, shBrushGrayFill);

	HPEN hOldPen = static_cast<HPEN>(SelectObject(hdcBuffer, shPenBlue));
	MoveToEx(hdcBuffer, sCopyButtonRect.left, sCopyButtonRect.top, nullptr);
	LineTo(hdcBuffer, sCopyButtonRect.right, sCopyButtonRect.top);
	LineTo(hdcBuffer, sCopyButtonRect.right, sCopyButtonRect.bottom);
	LineTo(hdcBuffer, sCopyButtonRect.left, sCopyButtonRect.bottom);
	LineTo(hdcBuffer, sCopyButtonRect.left, sCopyButtonRect.top);
	SelectObject(hdcBuffer, hOldPen);

	SetTextColor(hdcBuffer, RGB(200, 200, 200));
	TextOutA(hdcBuffer, sCopyButtonRect.left + 14, sCopyButtonRect.top + 2, "Copy", 4);
}

static void PaintTabBar(HDC hdcBuffer, int iWidth)
{
	for (int64_t i = 0; i < kiTabCount; ++i)
	{
		int iTabLeft = static_cast<int>(kiTabBarLeft + i * kiTabWidth);
		RECT tabRect {iTabLeft, 0, static_cast<int>(iTabLeft + kiTabWidth), static_cast<int>(kiTabHeight)};

		bool bActive = (i == static_cast<int64_t>(seActiveTab));
		HBRUSH hBrush = bActive ? shBrushTabActive : shBrushTabInactive;
		FillRect(hdcBuffer, &tabRect, hBrush);

		// Border
		HPEN hPen = bActive ? shPenBlue : shPenGray;
		HPEN hOldPen = static_cast<HPEN>(SelectObject(hdcBuffer, hPen));
		MoveToEx(hdcBuffer, iTabLeft, static_cast<int>(kiTabHeight), nullptr);
		LineTo(hdcBuffer, iTabLeft, 0);
		LineTo(hdcBuffer, static_cast<int>(iTabLeft + kiTabWidth), 0);
		LineTo(hdcBuffer, static_cast<int>(iTabLeft + kiTabWidth), static_cast<int>(kiTabHeight));
		if (!bActive)
		{
			LineTo(hdcBuffer, iTabLeft, static_cast<int>(kiTabHeight));
		}
		SelectObject(hdcBuffer, hOldPen);

		// Label
		SetTextColor(hdcBuffer, bActive ? RGB(255, 255, 255) : RGB(160, 160, 160));
		TextOutA(hdcBuffer, iTabLeft + 12, 4, kTabNames[i].data(), static_cast<int>(kTabNames[i].size()));
	}

	// Bottom line across non-tab area
	HPEN hOldPen = static_cast<HPEN>(SelectObject(hdcBuffer, shPenGray));
	int iTabsEnd = static_cast<int>(kiTabBarLeft + kiTabCount * kiTabWidth);
	MoveToEx(hdcBuffer, iTabsEnd, kiTabHeight, nullptr);
	LineTo(hdcBuffer, iWidth, kiTabHeight);
	SelectObject(hdcBuffer, hOldPen);
}

void PaintServerDisplay(HWND hWnd)
{
	PAINTSTRUCT ps {};
	HDC hdc = BeginPaint(hWnd, &ps);

	RECT clientRect {};
	GetClientRect(hWnd, &clientRect);
	int iWidth = clientRect.right - clientRect.left;
	int iHeight = clientRect.bottom - clientRect.top;

	EnsureCachedGdiObjects();

	// Persistent double-buffer: create the DC on first paint; recreate the bitmap only when the client size changes.
	if (shdcBuffer == nullptr)
	{
		shdcBuffer = CreateCompatibleDC(hdc);
	}
	if (shbmBuffer == nullptr || iWidth != siBufferWidth || iHeight != siBufferHeight)
	{
		HBITMAP hbmNew = CreateCompatibleBitmap(hdc, iWidth, iHeight);
		SelectObject(shdcBuffer, hbmNew); // deselects the previous bitmap (or the DC's default stock bitmap on first paint)
		if (shbmBuffer != nullptr)
		{
			DeleteObject(shbmBuffer);
		}
		shbmBuffer = hbmNew;
		siBufferWidth = iWidth;
		siBufferHeight = iHeight;
	}
	HDC hdcBuffer = shdcBuffer;

	// Fill background
	FillRect(hdcBuffer, &clientRect, shBrushBackground);

	// Select cached font
	SelectObject(hdcBuffer, shFont);
	SetBkMode(hdcBuffer, TRANSPARENT);

	// Read frame data from origin coord
	const game::Frame& rOriginFrame = game::gpGame->CurrentFrame(engine::kOriginCoord);
	int64_t iTick = rOriginFrame.interpolate.iTick;
	float fCurrentTime = rOriginFrame.interpolate.fCurrentTime;

	// Gather connected client info
	const std::vector<engine::ClientConnection>& rClients = engine::gpServer->mClients;
	int64_t iClientCount = static_cast<int64_t>(rClients.size());

	int64_t iActiveCells = static_cast<int64_t>(game::gpGame->mActiveCoords.size());

	// Left half: text stats
	int iTextX = 10;
	int iTextY = 10;
	int iLineHeight = 36;
	char pcLine[256] {};

	auto textLine = [&](const char* pcFormat, auto... args)
	{
		int iTextLength = std::min(std::snprintf(pcLine, sizeof(pcLine), pcFormat, args...), static_cast<int>(sizeof(pcLine)) - 1);
		TextOutA(hdcBuffer, iTextX, iTextY, pcLine, iTextLength);
		iTextY += iLineHeight;
	};

	// FPS
	SetTextColor(hdcBuffer, RGB(100, 180, 255));
	textLine("FPS: %lld", gpProfileManager->mFullUpdatesInTheLastSecond.Get());
	int64_t iFrameUpdateMicroseconds = gpProfileManager->GetCpuTimer(game::kCpuTimerFrameUpdate).smoothedMicroseconds.Average();
	int64_t iPotentialFramesPerSecond = iFrameUpdateMicroseconds > 0 ? 1'000'000 / iFrameUpdateMicroseconds : 0;
	textLine("Potential: %lld", iPotentialFramesPerSecond);
	iTextY += iLineHeight;

	SetTextColor(hdcBuffer, RGB(200, 200, 200));

	textLine("Tick: %lld", iTick);
	textLine("Time: %.2f s", static_cast<double>(fCurrentTime));
	textLine("Active cells: %lld", iActiveCells);
	int iTextLength = std::min(std::snprintf(pcLine, sizeof(pcLine), "Clients: %lld", iClientCount), static_cast<int>(sizeof(pcLine)) - 1);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, iTextLength);
	iTextY += iLineHeight;

	// Entity counts
	SetTextColor(hdcBuffer, RGB(150, 220, 150));

	textLine("Players: %lld", gpProfileManager->GetCpuCounter(game::kCpuCounterPlayers).iCount);
	textLine("Spaceships: %lld", gpProfileManager->GetCpuCounter(game::kCpuCounterSpaceships).iCount);
	textLine("Blasters: %lld", gpProfileManager->GetCpuCounter(game::kCpuCounterBlasters).iCount);
	textLine("Missiles: %lld", gpProfileManager->GetCpuCounter(game::kCpuCounterMissiles).iCount);
	textLine("Targets: %lld", gpProfileManager->GetCpuCounter(game::kCpuCounterTargets).iCount);
	iTextLength = std::min(std::snprintf(pcLine, sizeof(pcLine), "Explosions: %lld", gpProfileManager->GetCpuCounter(engine::kCpuCounterExplosions).iCount), static_cast<int>(sizeof(pcLine)) - 1);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, iTextLength);

#if !defined(ENABLE_CRT_DEBUG_HEAP)
	// Memory stats
	iTextY += iLineHeight * 2;
	SetTextColor(hdcBuffer, RGB(220, 180, 100));

	textLine("Committed: %lld MiB", gpProfileManager->miMimallocCommittedMib);
	textLine("Peak cmtd: %lld MiB", gpProfileManager->miMimallocPeakCommittedMib);
	textLine("Heap used: %lld MiB", gpProfileManager->miMimallocHeapUsedMib);
	iTextLength = std::min(std::snprintf(pcLine, sizeof(pcLine), "Peak heap: %lld MiB", gpProfileManager->miMimallocPeakHeapUsedMib), static_cast<int>(sizeof(pcLine)) - 1);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, iTextLength);
#endif

	// Tab bar
	PaintTabBar(hdcBuffer, iWidth);

	// Right side content area (below tab bar)
	int iContentLeft = static_cast<int>(kiTabBarLeft);
	int iContentTop = static_cast<int>(kiTabHeight) + 5;
	int iContentWidth = iWidth - iContentLeft - 10;
	int iContentHeight = iHeight - iContentTop - 10;

	if (seActiveTab == ServerTab::kMap)
	{
		PaintGridMap(hdcBuffer, pcLine, sizeof(pcLine), iContentLeft, iContentTop, iContentWidth, iContentHeight, rClients);
	}
	else if (seActiveTab == ServerTab::kProfile)
	{
		PaintProfilePanel(hdcBuffer, iContentLeft, iContentTop, iContentWidth, iContentHeight);
	}

	// Blit buffer to screen (buffer, font, and bitmap persist across paints — process teardown reclaims them)
	BitBlt(hdc, 0, 0, iWidth, iHeight, hdcBuffer, 0, 0, SRCCOPY);

	EndPaint(hWnd, &ps);
}

} // namespace game

#endif // BT_SERVER
