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

	// MainMenu is the one deliberately left-anchored column (its title composes over the live 3D scene)
	float fLeftPos = rIo.DisplaySize.x * kfMainMenuAnchorFractionX;
	float fTopPos = rIo.DisplaySize.y * kfMainMenuAnchorFractionY;
	ImGui::SetNextWindowPos(ImVec2(fLeftPos, fTopPos), ImGuiCond_Always);

	ScopedMenuFont menuFont;
	ImGui::Begin("MainMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

	// AlwaysAutoResize yields last-frame size — accepted one-frame lag
	ImVec2 vWindowPos = ImGui::GetWindowPos();
	ImVec2 vWindowSize = ImGui::GetWindowSize();
	DrawPanelBackground(ImGui::GetWindowDrawList(), vWindowPos, ImVec2(vWindowPos.x + vWindowSize.x, vWindowPos.y + vWindowSize.y));

	// Top breathing room above the title, symmetric with MenuHeading's gap below it
	ImGui::Dummy(ImVec2(0.0f, kfHeadingGapPixels * engine::UiScale()));
	MenuHeading("BROKEN ENGINE");

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;

	// Shared text-driven width over every button label (including the SCANNING... discovery state), floored to the
	// primary-button minimum. Height auto-sizes per button (0.0f).
	float fButtonWidth = std::max(MenuButtonsWidth({TranslatedString(kStringLocalServer), TranslatedString(kStringRemoteServer), TranslatedString(kStringGraphics), TranslatedString(kStringSound), TranslatedString(kStringQuit), U"SCANNING..."}), kfPrimaryButtonMinWidthPixels * engine::UiScale());

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
		if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringLocalServer)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[0]))
		{
			gpClientSession->ConnectToDiscoveredServer();
		}
	}
	else
	{
		ImGui::BeginDisabled();
		MenuButton("SCANNING...", ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[0]);
		ImGui::EndDisabled();
	}

	// Remote Server button (placeholder for future Internet servers)
	ImGui::BeginDisabled();
	MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringRemoteServer)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[1]);
	ImGui::EndDisabled();

	// Graphics button
	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringGraphics)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[2]))
	{
		gpGame->meUiState = UiState::kGraphicsSettings;
		engine::gSunAngleOverride.Set(gpCamera->RawSunAngle());
	}

	// Sound button
	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringSound)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[3]))
	{
		gpGame->meUiState = UiState::kSound;
	}

	// Quit button
	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringQuit)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[4]))
	{
		gpGame->mGameFlags.Set(engine::GameFlags::kQuit);
	}

	ImGui::End();

	// Language selection row: auto-sized window pivoted at bottom-center, hugging its buttons
	ImVec2 vLanguageAnchor(rIo.DisplaySize.x * 0.5f, rIo.DisplaySize.y - kfScreenBottomMarginPixels * engine::UiScale());
	ImGui::SetNextWindowPos(vLanguageAnchor, ImGuiCond_Always, ImVec2(0.5f, 1.0f));

	ScopedMenuFont languageFont(kfMenuUiScale * 0.75f);
	ImGui::Begin("LanguageMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);

	static constexpr const char* kpcLanguageNames[] = {"ENGLISH", "中文", "ESPANOL", "PORTUGUES", "FRANCAIS", "DEUTSCH"};
	static_assert(std::size(kpcLanguageNames) == static_cast<size_t>(kLanguageCount)); // One label per Language enumerator, in order.

	// Uniform text-driven width = max CalcTextSize over the 6 labels (中文 measured under the CJK font so it fits),
	// plus frame padding to match the button chrome. Buttons then sit one-per-row via SameLine (default spacing).
	float fLangButtonWidth = 0.0f;
	for (int64_t i = 0; i < kLanguageCount; ++i)
	{
		bool bChineseLabel = (i == kChinese) && (geLanguage != kChinese);
		if (bChineseLabel)
		{
			ImGui::PushFont(engine::gpImGuiManager->mpChineseFont, 0.0f);
		}
		fLangButtonWidth = std::max(fLangButtonWidth, ImGui::CalcTextSize(kpcLanguageNames[i]).x);
		if (bChineseLabel)
		{
			ImGui::PopFont();
		}
	}
	fLangButtonWidth += ImGui::GetStyle().FramePadding.x * 4.0f;

	for (int64_t i = 0; i < kLanguageCount; ++i)
	{
		if (i > 0)
		{
			ImGui::SameLine();
		}

		bool bSelected = (geLanguage == static_cast<Language>(i));

		// Use Chinese font for the Chinese button when in EFIGS mode (0.0f keeps the current size)
		bool bChineseButton = (i == kChinese) && (geLanguage != kChinese);
		if (bChineseButton)
		{
			ImGui::PushFont(engine::gpImGuiManager->mpChineseFont, 0.0f);
		}

		if (MenuButton(kpcLanguageNames[i], ImVec2(fLangButtonWidth, 0.0f), mfLanguageHoverAnims[i], bSelected))
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
