#if defined(BT_SERVER)

#include "Server/ServerDisplay.h"

#include "Network/NetworkServer/NetworkServer.h"

#include "Game.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Frame/Collections/Targets/Targets.h"
#include "Profile/ProfileManager.h"

namespace engine
{

void UpdateServerDisplayStats()
{
	if constexpr (!kbEnableProfiling)
	{
		return;
	}

	int64_t iTotalPlayers = 0;
	int64_t iTotalSpaceships = 0;
	int64_t iTotalBlasters = 0;
	int64_t iTotalMissiles = 0;
	int64_t iTotalTargets = 0;
	int64_t iTotalExplosions = 0;

	for (const GridCoord& rCoord : game::gpGame->mActiveCoords)
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
	gpProfileManager->SetCount(kCpuCounterExplosions, iTotalExplosions);

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
}

void PaintServerDisplay(HWND hWnd)
{
	PAINTSTRUCT ps {};
	HDC hdc = BeginPaint(hWnd, &ps);

	RECT clientRect {};
	GetClientRect(hWnd, &clientRect);
	int iWidth = clientRect.right - clientRect.left;
	int iHeight = clientRect.bottom - clientRect.top;

	// Double-buffer: paint to off-screen bitmap, then blit to screen
	HDC hdcBuffer = CreateCompatibleDC(hdc);
	HBITMAP hbmBuffer = CreateCompatibleBitmap(hdc, iWidth, iHeight);
	HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcBuffer, hbmBuffer));

	// Fill background
	HBRUSH hBrushBackground = CreateSolidBrush(RGB(30, 30, 30));
	FillRect(hdcBuffer, &clientRect, hBrushBackground);
	DeleteObject(hBrushBackground);

	// Create font
	HFONT hFont = CreateFont(28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
	HFONT hOldFont = static_cast<HFONT>(SelectObject(hdcBuffer, hFont));
	SetBkMode(hdcBuffer, TRANSPARENT);

	// Read frame data from origin coord
	const game::Frame& rOriginFrame = game::gpGame->CurrentFrame(kOriginCoord);
	int64_t iTick = rOriginFrame.interpolate.iTick;
	float fCurrentTime = rOriginFrame.interpolate.fCurrentTime;

	// Gather connected client info
	const std::vector<ClientConnection>& rClients = gpNetworkServer->GetClients();
	int64_t iClientCount = static_cast<int64_t>(rClients.size());

	int64_t iActiveCells = static_cast<int64_t>(game::gpGame->mActiveCoords.size());

	// Left half: text stats
	int iTextX = 10;
	int iTextY = 10;
	int iLineHeight = 36;
	char pcLine[256] {};

	SetTextColor(hdcBuffer, RGB(200, 200, 200));

	snprintf(pcLine, sizeof(pcLine), "Tick: %lld", iTick);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight;

	snprintf(pcLine, sizeof(pcLine), "Time: %.2f s", static_cast<double>(fCurrentTime));
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight;

	snprintf(pcLine, sizeof(pcLine), "Active cells: %lld", iActiveCells);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight;

	snprintf(pcLine, sizeof(pcLine), "Clients: %lld", iClientCount);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight * 2;

	// Entity counts
	SetTextColor(hdcBuffer, RGB(150, 220, 150));

	snprintf(pcLine, sizeof(pcLine), "Players: %lld", gpProfileManager->GetCpuCounter(game::kCpuCounterPlayers).iCount);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight;

	snprintf(pcLine, sizeof(pcLine), "Spaceships: %lld", gpProfileManager->GetCpuCounter(game::kCpuCounterSpaceships).iCount);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight;

	snprintf(pcLine, sizeof(pcLine), "Blasters: %lld", gpProfileManager->GetCpuCounter(game::kCpuCounterBlasters).iCount);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight;

	snprintf(pcLine, sizeof(pcLine), "Missiles: %lld", gpProfileManager->GetCpuCounter(game::kCpuCounterMissiles).iCount);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight;

	snprintf(pcLine, sizeof(pcLine), "Targets: %lld", gpProfileManager->GetCpuCounter(game::kCpuCounterTargets).iCount);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight;

	snprintf(pcLine, sizeof(pcLine), "Explosions: %lld", gpProfileManager->GetCpuCounter(kCpuCounterExplosions).iCount);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));

#if !defined(ENABLE_CRT_DEBUG_HEAP)
	// Memory stats
	iTextY += iLineHeight * 2;
	SetTextColor(hdcBuffer, RGB(220, 180, 100));

	snprintf(pcLine, sizeof(pcLine), "Committed: %lld MiB", gpProfileManager->miMimallocCommittedMib);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight;

	snprintf(pcLine, sizeof(pcLine), "Peak cmtd: %lld MiB", gpProfileManager->miMimallocPeakCommittedMib);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight;

	snprintf(pcLine, sizeof(pcLine), "Heap used: %lld MiB", gpProfileManager->miMimallocHeapUsedMib);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
	iTextY += iLineHeight;

	snprintf(pcLine, sizeof(pcLine), "Peak heap: %lld MiB", gpProfileManager->miMimallocPeakHeapUsedMib);
	TextOutA(hdcBuffer, iTextX, iTextY, pcLine, static_cast<int>(strlen(pcLine)));
#endif

	// Right side: grid map (fixed-width stats panel on the left)
	int iMapLeft = 250;
	int iMapTop = 10;
	int iMapWidth = iWidth - iMapLeft - 10;
	int iMapHeight = iHeight - 20;

	if (!game::gpGame->mActiveCoords.empty())
	{
		// Compute grid bounds with 1-cell padding
		int32_t iMinX = game::gpGame->mActiveCoords.at(0).x;
		int32_t iMaxX = iMinX;
		int32_t iMinY = game::gpGame->mActiveCoords.at(0).y;
		int32_t iMaxY = iMinY;

		for (const GridCoord& rCoord : game::gpGame->mActiveCoords)
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

		if (iCellSize >= 4)
		{
			for (int32_t gy = iMinY; gy <= iMaxY; ++gy)
			{
				for (int32_t gx = iMinX; gx <= iMaxX; ++gx)
				{
					int iCellLeft = iMapLeft + iOffsetX + (gx - iMinX) * iCellSize;
					int iCellTop = iMapTop + iOffsetY + (iMaxY - gy) * iCellSize;
					RECT cellRect {iCellLeft, iCellTop, iCellLeft + iCellSize, iCellTop + iCellSize};

					GridCoord coord {gx, gy};
					bool bIsActive = false;
					int64_t iClientsInCell = 0;

					for (const GridCoord& rActiveCoord : game::gpGame->mActiveCoords)
					{
						if (rActiveCoord == coord)
						{
							bIsActive = true;
							break;
						}
					}

					for (const ClientConnection& rClient : rClients)
					{
						if (rClient.humanGridCoord == coord && rClient.humanPlayerId.IsValid())
						{
							++iClientsInCell;
						}
					}

					// Fill cell
					COLORREF uiFillColor;
					if (iClientsInCell > 0)
					{
						uiFillColor = RGB(40, 60, 140);
					}
					else if (bIsActive)
					{
						uiFillColor = RGB(30, 100, 30);
					}
					else
					{
						uiFillColor = RGB(50, 50, 50);
					}

					HBRUSH hBrushCell = CreateSolidBrush(uiFillColor);
					FillRect(hdcBuffer, &cellRect, hBrushCell);
					DeleteObject(hBrushCell);

					// Border
					COLORREF uiBorderColor = (iClientsInCell > 0) ? RGB(100, 140, 255) : RGB(80, 80, 80);
					HPEN hPen = CreatePen(PS_SOLID, 1, uiBorderColor);
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdcBuffer, hPen));
					MoveToEx(hdcBuffer, iCellLeft, iCellTop, nullptr);
					LineTo(hdcBuffer, iCellLeft + iCellSize, iCellTop);
					LineTo(hdcBuffer, iCellLeft + iCellSize, iCellTop + iCellSize);
					LineTo(hdcBuffer, iCellLeft, iCellTop + iCellSize);
					LineTo(hdcBuffer, iCellLeft, iCellTop);
					SelectObject(hdcBuffer, hOldPen);
					DeleteObject(hPen);

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

						snprintf(pcLine, sizeof(pcLine), "(%d,%d)", gx, gy);
						TextOutA(hdcBuffer, iCellLeft + 2, iCellTop + 2, pcLine, static_cast<int>(strlen(pcLine)));

						snprintf(pcLine, sizeof(pcLine), "%lld", iEntityCount);
						TextOutA(hdcBuffer, iCellLeft + 2, iCellTop + 28, pcLine, static_cast<int>(strlen(pcLine)));

						if (iClientsInCell > 0)
						{
							SetTextColor(hdcBuffer, RGB(150, 200, 255));
							snprintf(pcLine, sizeof(pcLine), "%lld", iClientsInCell);
							TextOutA(hdcBuffer, iCellLeft + iCellSize - 20, iCellTop + 2, pcLine, static_cast<int>(strlen(pcLine)));
						}
					}
				}
			}
		}
	}

	SelectObject(hdcBuffer, hOldFont);
	DeleteObject(hFont);

	// Blit buffer to screen
	BitBlt(hdc, 0, 0, iWidth, iHeight, hdcBuffer, 0, 0, SRCCOPY);

	// Cleanup buffer
	SelectObject(hdcBuffer, hbmOld);
	DeleteObject(hbmBuffer);
	DeleteDC(hdcBuffer);

	EndPaint(hWnd, &ps);
}

} // namespace engine

#endif
