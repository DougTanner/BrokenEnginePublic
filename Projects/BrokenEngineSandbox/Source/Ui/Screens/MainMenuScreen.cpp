#include "MainMenuScreen.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "MenuUtils.h"
#include "Network/Client/ClientSession.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
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

	// Invisible window for main menu (panel chrome is custom-drawn below)
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

	// Position: 11% from left, 35% from top
	float fLeftPos = rIo.DisplaySize.x * 0.11f;
	float fTopPos = rIo.DisplaySize.y * 0.35f;
	ImGui::SetNextWindowPos(ImVec2(fLeftPos, fTopPos), ImGuiCond_Always);

	ScopedMenuFont menuFont;
	ImGui::Begin("MainMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

	// AlwaysAutoResize yields last-frame size — accepted one-frame lag (same pattern as HudScreen vLastSize)
	ImVec2 vWindowPos = ImGui::GetWindowPos();
	ImVec2 vWindowSize = ImGui::GetWindowSize();
	DrawPanelBackground(ImGui::GetWindowDrawList(), vWindowPos, ImVec2(vWindowPos.x + vWindowSize.x, vWindowPos.y + vWindowSize.y));

	{
		ScopedMenuFont headingFont(kfMenuUiScale * kfMenuHeadingScale);
		ImGui::TextUnformatted("BROKEN ENGINE");
	}
	ImGui::Dummy(ImVec2(0.0f, rIo.DisplaySize.y * 0.01f));

	float fButtonWidth = rIo.DisplaySize.x * 0.2f;
	float fButtonHeight = rIo.DisplaySize.y * 0.045f;
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;

	// Auto-start discovery when main menu is shown
	if (gpClientSession->mpDiscoveryScanner == nullptr && !(gpClientSession->mSessionFlags & engine::SessionStateFlags::kServerDiscovered))
	{
		gpClientSession->StartServerDiscovery();
	}

	// Auto-launch server
	if constexpr (kbAutoRunServer)
	{
		static bool sbServerLaunched = false;
		if (!sbServerLaunched && (gpClientSession->mSessionFlags & engine::SessionStateFlags::kDiscoveryScanTimedOut))
		{
			sbServerLaunched = true;
			// Heap: std::filesystem::path allocates; one-time server launch path
			ScopedSuppressAllocationTracking suppress;
			wchar_t pcPath[MAX_PATH] {};
			GetModuleFileNameW(nullptr, pcPath, static_cast<DWORD>(std::size(pcPath) - 1));
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
		if (!sbConnected && (gpClientSession->mSessionFlags & engine::SessionStateFlags::kServerDiscovered))
		{
			sbConnected = true;
			gpClientSession->ConnectToDiscoveredServer();
		}
	}

	// Local Server button (discovers localhost + LAN)
	if (gpClientSession->mSessionFlags & engine::SessionStateFlags::kServerDiscovered)
	{
		if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringLocalServer)), ImVec2(fButtonWidth, fButtonHeight), mfButtonHoverAnims[0]))
		{
			gpClientSession->ConnectToDiscoveredServer();
		}
	}
	else
	{
		ImGui::BeginDisabled();
		MenuButton("SCANNING...", ImVec2(fButtonWidth, fButtonHeight), mfButtonHoverAnims[0]);
		ImGui::EndDisabled();
	}

	// Remote Server button (placeholder for future Internet servers)
	ImGui::BeginDisabled();
	MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringRemoteServer)), ImVec2(fButtonWidth, fButtonHeight), mfButtonHoverAnims[1]);
	ImGui::EndDisabled();

	// Graphics button
	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringGraphics)), ImVec2(fButtonWidth, fButtonHeight), mfButtonHoverAnims[2]))
	{
		gpGame->meUiState = UiState::kGraphicsSettings;
		engine::gSunAngleOverride.Set(gpCamera->RawSunAngle());
	}

	// Sound button
	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringSound)), ImVec2(fButtonWidth, fButtonHeight), mfButtonHoverAnims[3]))
	{
		gpGame->meUiState = UiState::kSound;
	}

	// Quit button
	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringQuit)), ImVec2(fButtonWidth, fButtonHeight), mfButtonHoverAnims[4]))
	{
		gpGame->mGameFlags.Set(engine::GameFlags::kQuit);
	}

	ImGui::End();

	// Language selection row at bottom
	float fLanguageRowHeight = rIo.DisplaySize.y * 0.05f;
	float fLanguageY = rIo.DisplaySize.y - fLanguageRowHeight - rIo.DisplaySize.y * 0.02f;
	ImGui::SetNextWindowPos(ImVec2(0.0f, fLanguageY), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(rIo.DisplaySize.x, fLanguageRowHeight));

	ScopedMenuFont languageFont(kfMenuUiScale * 0.75f);
	ImGui::Begin("LanguageMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

	float fLangButtonWidth = rIo.DisplaySize.x * 0.06f;
	float fLangButtonHeight = rIo.DisplaySize.y * 0.029f;
	float fLangButtonSpacing = rIo.DisplaySize.x * 0.01f;

	// Center the language buttons (6 buttons + 5 spacings)
	float fTotalLangWidth = fLangButtonWidth * 6.0f + fLangButtonSpacing * 5.0f;
	ImGui::SetCursorPosX((rIo.DisplaySize.x - fTotalLangWidth) / 2.0f);

	static constexpr const char* kpcLanguageNames[] = {"ENGLISH", "中文", "ESPANOL", "PORTUGUES", "FRANCAIS", "DEUTSCH"};
	static_assert(std::size(kpcLanguageNames) == static_cast<size_t>(kLanguageCount)); // One label per Language enumerator, in order.
	for (int64_t i = 0; i < kLanguageCount; ++i)
	{
		if (i > 0)
		{
			ImGui::SameLine(0.0f, fLangButtonSpacing);
		}

		bool bSelected = (geLanguage == static_cast<Language>(i));

		// Use Chinese font for the Chinese button when in EFIGS mode (0.0f keeps the current size)
		bool bChineseButton = (i == kChinese) && (geLanguage != kChinese);
		if (bChineseButton)
		{
			ImGui::PushFont(engine::gpImGuiManager->mpChineseFont, 0.0f);
		}

		if (MenuButton(kpcLanguageNames[i], ImVec2(fLangButtonWidth, fLangButtonHeight), mfLanguageHoverAnims[i], bSelected))
		{
			geLanguage = static_cast<Language>(i);
		}

		if (bChineseButton)
		{
			ImGui::PopFont();
		}
	}

	ImGui::End();

	ImGui::PopStyleColor(2);
}

} // namespace game

#endif // BT_CLIENT
