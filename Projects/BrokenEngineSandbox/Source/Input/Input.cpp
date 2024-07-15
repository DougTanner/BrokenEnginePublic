#include "Input.h"

#include "Ui/Wrapper.h"
#include "Frame/Render.h"

#include "Game.h"

using namespace DirectX;

namespace game
{

using enum MenuInputFlags;

std::tuple<MenuInput, FrameInput> ProcessRawInput(const engine::RawInput& rRawInput)
{
	MenuInput menuInput {};
	FrameInput frameInput {};

	frameInput.f4VisibleTopLeft = engine::gf4VisibleTopLeft;
	frameInput.f4VisibleTopRight = engine::gf4VisibleTopRight;
	frameInput.f4VisibleBottomLeft = engine::gf4VisibleBottomLeft;
	frameInput.f4VisibleBottomRight = engine::gf4VisibleBottomRight;
	frameInput.f4LargeVisibleArea = engine::gf4LargeVisibleArea;

	static constexpr float kfGamepadThreshold = 0.1f;
	static bool sbGamepadMode = false;
	bool bKeyboardMouse = rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft].IsDown() || rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonRight].IsDown() ||
	                      rRawInput.pKeyboardKeys['A'].IsDown() || rRawInput.pKeyboardKeys['D'].IsDown() || rRawInput.pKeyboardKeys['W'].IsDown() || rRawInput.pKeyboardKeys['S'].IsDown() ||
		                  rRawInput.pKeyboardKeys[VK_LEFT].IsDown() || rRawInput.pKeyboardKeys[VK_RIGHT].IsDown() || rRawInput.pKeyboardKeys[VK_UP].IsDown() || rRawInput.pKeyboardKeys[VK_DOWN].IsDown() ||
		                  rRawInput.pKeyboardKeys[VK_NUMPAD1].IsDown() || rRawInput.pKeyboardKeys[VK_NUMPAD3].IsDown() || rRawInput.pKeyboardKeys[VK_NUMPAD5].IsDown() || rRawInput.pKeyboardKeys[VK_NUMPAD2].IsDown();
	if (bKeyboardMouse)
	{
		sbGamepadMode = false;
	}
	else if (std::abs(rRawInput.f2LeftThumbstick.x + rRawInput.f2LeftThumbstick.y) > kfGamepadThreshold || std::abs(rRawInput.f2RightThumbstick.x + rRawInput.f2RightThumbstick.y) > kfGamepadThreshold)
	{
		sbGamepadMode = true;
	}
	else
	{
		static DirectX::XMFLOAT2 sf2MousePosition {};
		if (rRawInput.f2MousePosition != sf2MousePosition)
		{
			sbGamepadMode = false;
		}
		sf2MousePosition = rRawInput.f2MousePosition;
	}

	menuInput.bGamepad = sbGamepadMode;
	frameInput.held.bGamepad = sbGamepadMode;

	// Menu
	menuInput.flags.Set(kQuit, rRawInput.pKeyboardKeys[VK_MENU].IsDown() && rRawInput.pKeyboardKeys[VK_F4].WasPressed());
	menuInput.flags.Set(kToggleFullscreen, rRawInput.pKeyboardKeys[VK_F1].WasPressed());
	menuInput.flags.Set(kMouseIsDown, rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft].IsDown());
	menuInput.flags.Set(kMouseClick, rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft].WasPressed());
	menuInput.flags.Set(kGamepadButton, rRawInput.pGamepadButtons[engine::GamepadButtons::kGamepadButtonA].WasPressed());
#if defined(ENABLE_DEBUG_INPUT)
	menuInput.flags.Set(kQuit, rRawInput.pKeyboardKeys[VK_F4].WasPressed());
	menuInput.flags.Set(kToggleProfileText, rRawInput.pKeyboardKeys['P'].WasPressed());
	menuInput.flags.Set(kTogglePauseFrame, rRawInput.pKeyboardKeys[VK_SPACE].WasPressed());
	menuInput.flags.Set(kResetFrame, rRawInput.pKeyboardKeys[VK_RETURN].WasPressed());
	menuInput.flags.Set(kQuicksave, rRawInput.pKeyboardKeys[VK_F5].WasPressed());
	menuInput.flags.Set(kQuickload, rRawInput.pKeyboardKeys[VK_F6].WasPressed());
	menuInput.flags.Set(kSaveReplay, rRawInput.pKeyboardKeys[VK_F7].WasPressed());
	menuInput.flags.Set(kLoadReplay, rRawInput.pKeyboardKeys[VK_F8].WasPressed());
	menuInput.flags.Set(kSlowTime, rRawInput.pKeyboardKeys[VK_OEM_MINUS].WasPressed());
	menuInput.flags.Set(kSpeedUpTime, rRawInput.pKeyboardKeys[VK_OEM_PLUS].WasPressed());
	menuInput.flags.Set(kSingleStep, rRawInput.pKeyboardKeys[VK_TAB].WasPressed());
#endif
#if defined(ENABLE_SCREENSHOTS)
	menuInput.flags.Set(kToggleScreenshots, rRawInput.pKeyboardKeys[VK_F9].WasPressed());
#endif

	menuInput.f2Mouse = rRawInput.f2MousePosition;
	menuInput.f2Gamepad = rRawInput.f2LeftThumbstick;

	// Menus
	menuInput.flags.Set(kPauseMenu, rRawInput.pKeyboardKeys[VK_ESCAPE].WasPressed()
	                             || rRawInput.pMouseButtons[engine::kMouseButtonMiddle].WasPressed()
	                             || rRawInput.pGamepadButtons[engine::kGamepadMenu].WasPressed()
	                             || rRawInput.pGamepadButtons[engine::kGamepadButtonB].WasPressed());
#if defined(ENABLE_DEBUG_INPUT)
	menuInput.flags.Set(kMenuGraphics, rRawInput.pKeyboardKeys[VK_F2].WasPressed());
	menuInput.flags.Set(kMenuTweaks, rRawInput.pKeyboardKeys[VK_F3].WasPressed());
#endif

	// No frame input in main menu
	if (gpGame->InMainMenu())
	{
		return std::tuple(std::move(menuInput), std::move(frameInput));
	}

	// Gamepad
	auto vecGamepadDirection = XMVectorSet(rRawInput.f2RightThumbstick.x, rRawInput.f2RightThumbstick.y, 0.0f, 0.0f);
	float fGamepadMagnitude = XMVectorGetX(XMVector3Length(vecGamepadDirection));
	static XMVECTOR sPreviousGamepadDirection = {1.0f, 0.0f, 0.0f, 0.0f};
	if (fGamepadMagnitude > kfGamepadThreshold)
	{
		vecGamepadDirection = XMVector3Normalize(vecGamepadDirection);
		sPreviousGamepadDirection = vecGamepadDirection;
	}
	else
	{
		vecGamepadDirection = sPreviousGamepadDirection;
	}

	// Blaster
	frameInput.pressedFlags.Set(FrameInputPressedFlags::kTogglePrimary, rRawInput.pMouseButtons[engine::kMouseButtonLeft].WasPressed());

	if (rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft].IsDown())
	{
		if (gpGame->meUiState == UiState::kNone)
		{
			frameInput.held.flags |= FrameInputHeldFlags::kPrimary;
		}
	}
	else if (fGamepadMagnitude > kfGamepadThreshold)
	{
		frameInput.held.flags |= FrameInputHeldFlags::kPrimary;
	}

	// Missile
	frameInput.pressedFlags.Set(FrameInputPressedFlags::kToggleSecondary, rRawInput.pMouseButtons[engine::kMouseButtonRight].WasPressed());

	if (rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonRight].IsDown())
	{
		frameInput.held.flags |= FrameInputHeldFlags::kSecondary;
	}
	else if (rRawInput.f2Triggers.y > kfGamepadThreshold)
	{
		frameInput.held.flags |= FrameInputHeldFlags::kSecondary;
	}

	// Firing direction
	auto vecMouseDirection = engine::ScreenToWorld(XMVectorSet(rRawInput.f2MousePosition.x, rRawInput.f2MousePosition.y, 0.0f, 0.0f), engine::gBaseHeight.Get()) - gpGame->CurrentFrame().player.vecPosition;
	frameInput.held.vecDirection = XMVector3Normalize(sbGamepadMode ? vecGamepadDirection : vecMouseDirection);

	if (sbGamepadMode)
	{
		frameInput.held.f2MovePlayer.x = 1.0f * rRawInput.f2LeftThumbstick.x;
		frameInput.held.f2MovePlayer.y = 1.0f * rRawInput.f2LeftThumbstick.y;
	}
	else
	{
		frameInput.held.f2MovePlayer.x = rRawInput.pKeyboardKeys['A'].IsDown() ? -1.0f : (rRawInput.pKeyboardKeys['D'].IsDown() ? 1.0f : 0.0f);
		frameInput.held.f2MovePlayer.y = rRawInput.pKeyboardKeys['W'].IsDown() ? 1.0f : (rRawInput.pKeyboardKeys['S'].IsDown() ? -1.0f : 0.0f);

		frameInput.held.f2MovePlayer.x += rRawInput.pKeyboardKeys[VK_LEFT].IsDown() ? -1.0f : (rRawInput.pKeyboardKeys[VK_RIGHT].IsDown() ? 1.0f : 0.0f);
		frameInput.held.f2MovePlayer.y += rRawInput.pKeyboardKeys[VK_UP].IsDown() ? 1.0f : (rRawInput.pKeyboardKeys[VK_DOWN].IsDown() ? -1.0f : 0.0f);

		frameInput.held.f2MovePlayer.x += rRawInput.pKeyboardKeys[VK_NUMPAD1].IsDown() ? -1.0f : (rRawInput.pKeyboardKeys[VK_NUMPAD3].IsDown() ? 1.0f : 0.0f);
		frameInput.held.f2MovePlayer.y += rRawInput.pKeyboardKeys[VK_NUMPAD5].IsDown() ? 1.0f : (rRawInput.pKeyboardKeys[VK_NUMPAD2].IsDown() ? -1.0f : 0.0f);
	}
	frameInput.held.f2MovePlayer.x = std::clamp(frameInput.held.f2MovePlayer.x, -1.0f, 1.0f);
	frameInput.held.f2MovePlayer.y = std::clamp(frameInput.held.f2MovePlayer.y, -1.0f, 1.0f);

	// Skill two
	static float sfPreviousTriggerX = 0.0f;
	frameInput.pressedFlags.Set(FrameInputPressedFlags::kToggleSkill, rRawInput.pKeyboardKeys['E'].WasPressed() ||
#if !defined(ENABLE_DEBUG_INPUT)
	                                                                  rRawInput.pKeyboardKeys[VK_SPACE].WasPressed() ||
#endif
	                                                                  rRawInput.pKeyboardKeys[VK_NUMPAD0].WasPressed() ||
	                                                                  rRawInput.pGamepadButtons[engine::kGamepadRightShoulder].WasPressed() ||
	                                                                  (sfPreviousTriggerX < kfGamepadThreshold && rRawInput.f2Triggers.x >= kfGamepadThreshold) ||
	                                                                  rRawInput.iScrollWheel != 0);
	sfPreviousTriggerX = rRawInput.f2Triggers.x;

#if defined(ENABLE_DEBUG_INPUT)
	if (rRawInput.pKeyboardKeys[VK_OEM_6].IsDown())
	{
		frameInput.held.flags |= FrameInputHeldFlags::kZoomOut;
	}
	else if (rRawInput.pKeyboardKeys[VK_OEM_4].IsDown())
	{
		frameInput.held.flags |= FrameInputHeldFlags::kZoomIn;
	}
#endif

	return std::tuple(std::move(menuInput), std::move(frameInput));
}

} // namespace game
