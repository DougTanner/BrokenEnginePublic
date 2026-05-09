#include "MainMenuScreen.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "MenuUtils.h"
#include "Network/Client/ClientSession.h"
#include "Ui/Localization.h"

namespace game
{

void MainMenuScreen::Render()
{
	if (gpGame->meUiState != UiState::kPause || !gpGame->InMainMenu())
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	// Use Chinese font for all menu text when language is Chinese
	bool bChineseMode = (geLanguage == kChinese);
	if (bChineseMode)
	{
		ImGui::PushFont(engine::gpImGuiManager->mpChineseFont);
	}

	// Invisible window for main menu
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

	// Position: 11% from left, 35% from top
	float fLeftPos = rIo.DisplaySize.x * 0.11f;
	float fTopPos = rIo.DisplaySize.y * 0.35f;
	ImGui::SetNextWindowPos(ImVec2(fLeftPos, fTopPos), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(rIo.DisplaySize.x * 0.4f, 0.0f));

	ImGui::Begin("MainMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfMenuUiScale);

	float fButtonWidth = rIo.DisplaySize.x * 0.2f;
	float fButtonHeight = rIo.DisplaySize.y * 0.045f;
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;

	// Auto-start discovery when main menu is shown
	if (gpClientSession->mpDiscoveryScanner == nullptr && !gpClientSession->mbServerDiscovered)
	{
		gpClientSession->StartServerDiscovery();
	}

	// Auto-launch server
	if constexpr (kbAutoRunServer)
	{
		static bool sbServerLaunched = false;
		if (!sbServerLaunched && gpClientSession->mbDiscoveryScanTimedOut)
		{
			sbServerLaunched = true;
			// Heap: std::filesystem::path allocates; one-time server launch path
			ScopedSuppressAllocationTracking suppress;
			char pcPath[MAX_PATH] {};
			GetModuleFileName(nullptr, pcPath, static_cast<DWORD>(std::size(pcPath) - 1));
			std::filesystem::path serverPath = pcPath;
			serverPath.remove_filename();
			if constexpr (std::string_view(kpcBuildConfigName) == "Release")
			{
				serverPath /= "BrokenEngineSandboxServer.exe";
			}
			else
			{
				serverPath /= std::format("BrokenEngineSandboxServer.{}.exe", kpcBuildConfigName);
			}
			common::LaunchExecutable(serverPath);
		}
	}

	// Auto-connect
	if constexpr (kbAutoConnect)
	{
		static bool sbConnected = false;
		if (!sbConnected && gpClientSession->mbServerDiscovered)
		{
			sbConnected = true;
			gpClientSession->ConnectToDiscoveredServer();
		}
	}

	// Local Server button (discovers localhost + LAN)
	if (gpClientSession->mbServerDiscovered)
	{
		if (ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringLocalServer)), ImVec2(fButtonWidth, fButtonHeight)))
		{
			gpClientSession->ConnectToDiscoveredServer();
		}
	}
	else
	{
		ImGui::BeginDisabled();
		ImGui::Button("SCANNING...", ImVec2(fButtonWidth, fButtonHeight));
		ImGui::EndDisabled();
	}

	// Remote Server button (placeholder for future Internet servers)
	ImGui::BeginDisabled();
	ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringRemoteServer)), ImVec2(fButtonWidth, fButtonHeight));
	ImGui::EndDisabled();

	// Graphics button
	if (ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringGraphics)), ImVec2(fButtonWidth, fButtonHeight)))
	{
		gpGame->meUiState = UiState::kGraphicsSettings;
		engine::gSunAngleOverride.Set(gpCamera->RawSunAngle());
	}

	// Sound button
	if (ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringSound)), ImVec2(fButtonWidth, fButtonHeight)))
	{
		gpGame->meUiState = UiState::kSound;
	}

	// Quit button
	if (ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringQuit)), ImVec2(fButtonWidth, fButtonHeight)))
	{
		gpGame->mGameFlags.Set(engine::GameFlags::kQuit);
	}

	ImGui::End();

	// Language selection row at bottom
	float fLanguageRowHeight = rIo.DisplaySize.y * 0.05f;
	float fLanguageY = rIo.DisplaySize.y - fLanguageRowHeight - rIo.DisplaySize.y * 0.02f;
	ImGui::SetNextWindowPos(ImVec2(0.0f, fLanguageY), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(rIo.DisplaySize.x, fLanguageRowHeight));

	ImGui::Begin("LanguageMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
	ImGui::SetWindowFontScale(kfMenuUiScale * 0.75f);

	float fLangButtonWidth = rIo.DisplaySize.x * 0.06f;
	float fLangButtonHeight = rIo.DisplaySize.y * 0.029f;
	float fLangButtonSpacing = rIo.DisplaySize.x * 0.01f;

	// Center the language buttons (6 buttons + 5 spacings)
	float fTotalLangWidth = fLangButtonWidth * 6.0f + fLangButtonSpacing * 5.0f;
	ImGui::SetCursorPosX((rIo.DisplaySize.x - fTotalLangWidth) / 2.0f);

	static constexpr const char* kpcLanguageNames[] = {"ENGLISH", "中文", "ESPANOL", "PORTUGUES", "FRANCAIS", "DEUTSCH"};
	for (int64_t i = 0; i < kLanguageCount; ++i)
	{
		if (i > 0)
		{
			ImGui::SameLine(0.0f, fLangButtonSpacing);
		}

		bool bSelected = (geLanguage == static_cast<Language>(i));
		if (bSelected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.93f, 0.93f, 0.93f, 0.33f));
		}

		// Use Chinese font for the Chinese button when in EFIGS mode
		bool bChineseButton = (i == kChinese) && !bChineseMode;
		if (bChineseButton)
		{
			ImGui::PushFont(engine::gpImGuiManager->mpChineseFont);
		}

		if (ImGui::Button(kpcLanguageNames[i], ImVec2(fLangButtonWidth, fLangButtonHeight)))
		{
			geLanguage = static_cast<Language>(i);
		}

		if (bChineseButton)
		{
			ImGui::PopFont();
		}

		if (bSelected)
		{
			ImGui::PopStyleColor();
		}
	}

	ImGui::End();

	ImGui::PopStyleColor(2);

	if (bChineseMode)
	{
		ImGui::PopFont();
	}
}

} // namespace game

#endif // BT_CLIENT
