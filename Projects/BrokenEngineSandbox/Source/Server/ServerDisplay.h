#pragma once

#if defined(BT_SERVER)

namespace game
{

void ServerUpdateDisplayStats();
void PaintServerDisplay(HWND hWnd);
void HandleServerClick(HWND hWnd, int64_t iX, int64_t iY);

} // namespace game

#endif // BT_SERVER
