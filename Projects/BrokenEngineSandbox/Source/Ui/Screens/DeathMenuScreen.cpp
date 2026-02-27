#include "DeathMenuScreen.h"

#include "Game.h"
#include "MenuUtils.h"
#include "Ui/Localization.h"

namespace game
{

void DeathMenuScreen::Render()
{
	if (gpGame->meUiState != UiState::kNone)
	{
		return;
	}

	if (!(gpGame->CurrentFrame(gpGame->mHumanGridCoord).interpolate.gameFlags & GameFlags::kDeathScreen))
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	// Transparent window background
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

	float fWindowWidth = rIo.DisplaySize.x * 0.5f;
	ImGui::SetNextWindowPos(ImVec2((rIo.DisplaySize.x - fWindowWidth) / 2.0f, rIo.DisplaySize.y * 0.1f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(fWindowWidth, 0.0f));

	ImGui::Begin("DeathMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfMenuUiScale * 1.5f);

	// Game Over text
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	const char* pcGameOverText = AppendUtf8(rWorkbuffer, TranslatedString(kStringGameOver));
	float fTextWidth = ImGui::CalcTextSize(pcGameOverText).x;
	ImGui::SetCursorPosX((fWindowWidth - fTextWidth) / 2.0f);
	ImGui::TextUnformatted(pcGameOverText);

	ImGui::SetWindowFontScale(kfMenuUiScale);

	ImGui::Dummy(ImVec2(0.0f, rIo.DisplaySize.y * 0.05f));

	// Respawn button (centered)
	float fButtonWidth = rIo.DisplaySize.x * 0.2f;
	ImGui::SetCursorPosX((fWindowWidth - fButtonWidth) / 2.0f);
	if (ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringRespawn)), ImVec2(fButtonWidth, rIo.DisplaySize.y * 0.045f)))
	{
#ifdef BT_CLIENT
		if (gpGame->IsNetworkMode())
		{
			engine::gpNetworkClient->SendSpawnRequest(engine::ClientRequestFlags::kRespawnRequested);
		}
		else
#endif
		{
			gpGame->mSpawnFlags.Set(SpawnFlags::kRespawnRequested);
		}
	}

	ImGui::End();

	ImGui::PopStyleColor(2);
}

} // namespace game
