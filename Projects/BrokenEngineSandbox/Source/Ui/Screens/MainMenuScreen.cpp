#include "MainMenuScreen.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "MenuUtils.h"
#include "Network/Client/ClientSession.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/Localization.h"

namespace game
{

namespace
{

void CenterMenuItem(float fContentStartX, float fContentWidth, float fItemWidth)
{
	ImGui::SetCursorPosX(fContentStartX + (fContentWidth - fItemWidth) * 0.5f);
}

} // namespace

void MainMenuScreen::Render()
{
	if (gpGame->meUiState != UiState::kPause || !gpGame->InMainMenu())
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	// Invisible windows let the menu compose directly over the live scene
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

	// Center the title/action group vertically and place it on the screen's first vertical third.
	ImVec2 vMenuCenter(rIo.DisplaySize.x * kfMainMenuCenterFractionX, rIo.DisplaySize.y * kfMainMenuCenterFractionY);
	vMenuCenter.y -= kfMainMenuOpticalOffsetYPixels * engine::UiScale() * ImGui::GetStyle().FontScaleMain;
	ImGui::SetNextWindowPos(vMenuCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	ScopedMenuFont menuFont;
	ImGui::Begin("MainMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;

	// Shared text-driven width over every button label (including the SCANNING... discovery state), floored to the
	// primary-button minimum. Height auto-sizes per button (0.0f).
	float fButtonWidth = std::max(MenuButtonsWidth({TranslatedString(kStringLocalServer), TranslatedString(kStringRemoteServer), TranslatedString(kStringGraphics), TranslatedString(kStringAudio), TranslatedString(kStringGameSettings), TranslatedString(kStringQuit), U"SCANNING..."}), kfPrimaryButtonMinWidthPixels * engine::UiScale());
	float fHeadingWidth = 0.0f;
	{
		ScopedMenuFont headingFont(kfMenuUiScale * kfMainMenuHeadingScale);
		fHeadingWidth = ImGui::CalcTextSize("BROKEN ENGINE").x;
	}

	// Title and actions share one measured content width and center line, while the block remains scene-anchored.
	float fContentStartX = ImGui::GetCursorPosX();
	float fContentWidth = std::max(fHeadingWidth, fButtonWidth);
	CenterMenuItem(fContentStartX, fContentWidth, fHeadingWidth);
	MenuHeading("BROKEN ENGINE", kfMainMenuHeadingScale);

	// Auto-start discovery when main menu is shown
	if (gpClientSession->mpRuntime->mpDiscoveryScanner == nullptr && !(gpClientSession->mpRuntime->mStateFlags & engine::ClientSessionStateFlags::kServerDiscovered))
	{
		gpClientSession->mpRuntime->StartDiscovery();
	}

	// Auto-launch server
	if constexpr (kbAutoRunServer)
	{
		static bool sbServerLaunched = false;
		if (!sbServerLaunched && (gpClientSession->mpRuntime->mStateFlags & engine::ClientSessionStateFlags::kDiscoveryScanTimedOut))
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
		if (!sbConnected && (gpClientSession->mpRuntime->mStateFlags & engine::ClientSessionStateFlags::kServerDiscovered))
		{
			sbConnected = true;
			gpClientSession->mpRuntime->ConnectToDiscoveredServer(engine::kuiDefaultPort, NetworkSessionContract::kiCoordSlots);
		}
	}

	// Local Server button (discovers localhost + LAN)
	if (gpClientSession->mpRuntime->mStateFlags & engine::ClientSessionStateFlags::kServerDiscovered)
	{
		CenterMenuItem(fContentStartX, fContentWidth, fButtonWidth);
		if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringLocalServer)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[0]))
		{
			gpClientSession->mpRuntime->ConnectToDiscoveredServer(engine::kuiDefaultPort, NetworkSessionContract::kiCoordSlots);
		}
	}
	else
	{
		ImGui::BeginDisabled();
		CenterMenuItem(fContentStartX, fContentWidth, fButtonWidth);
		MenuButton("SCANNING...", ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[0]);
		ImGui::EndDisabled();
	}

	// Remote Server button (placeholder for future Internet servers)
	ImGui::BeginDisabled();
	CenterMenuItem(fContentStartX, fContentWidth, fButtonWidth);
	MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringRemoteServer)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[1]);
	ImGui::EndDisabled();

	// Graphics button
	CenterMenuItem(fContentStartX, fContentWidth, fButtonWidth);
	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringGraphics)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[2]))
	{
		gpGame->meUiState = UiState::kGraphicsSettings;
		engine::gSunAngleOverride.Set(gpCamera->RawSunAngle());
	}

	// Audio button
	CenterMenuItem(fContentStartX, fContentWidth, fButtonWidth);
	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringAudio)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[3]))
	{
		gpGame->meUiState = UiState::kSound;
	}

	// Game Settings button
	CenterMenuItem(fContentStartX, fContentWidth, fButtonWidth);
	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringGameSettings)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[4]))
	{
		gpGame->meUiState = UiState::kGameSettings;
	}

	// Quit button
	CenterMenuItem(fContentStartX, fContentWidth, fButtonWidth);
	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringQuit)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[5]))
	{
		gpGame->mGameFlags.Set(engine::GameFlags::kQuit);
	}

	ImGui::End();

	ImGui::PopStyleColor(2);
}

} // namespace game

#endif // BT_CLIENT
