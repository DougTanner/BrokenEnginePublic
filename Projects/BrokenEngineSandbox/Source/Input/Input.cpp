#include "Input.h"

#include "Ui/Wrapper.h"
#include "Frame/Render.h"

#include "Game.h"


namespace game
{

using enum MenuInputFlags;

constexpr float kfGamepadThreshold = 0.1f;

bool Input::UpdateMenuInput(const engine::RawInput& rRawInput)
{
	// Detect if keyboard or mouse is being used
	bool bKeyboardMouse = rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft] || rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonRight] || rRawInput.pKeyboardKeys['A'] || rRawInput.pKeyboardKeys['D'] || rRawInput.pKeyboardKeys['W'] || rRawInput.pKeyboardKeys['S'] || rRawInput.pKeyboardKeys[VK_LEFT] || rRawInput.pKeyboardKeys[VK_RIGHT] || rRawInput.pKeyboardKeys[VK_UP] || rRawInput.pKeyboardKeys[VK_DOWN] || rRawInput.pKeyboardKeys[VK_NUMPAD1] || rRawInput.pKeyboardKeys[VK_NUMPAD3] || rRawInput.pKeyboardKeys[VK_NUMPAD5] || rRawInput.pKeyboardKeys[VK_NUMPAD2];
	if (bKeyboardMouse)
	{
		mbGamepadMode = false;
	}
	else if (std::abs(rRawInput.f2LeftThumbstick.x + rRawInput.f2LeftThumbstick.y) > kfGamepadThreshold || std::abs(rRawInput.f2RightThumbstick.x + rRawInput.f2RightThumbstick.y) > kfGamepadThreshold)
	{
		mbGamepadMode = true;
	}
	else
	{
		static XMFLOAT2 sf2MousePosition {};
		if (rRawInput.f2MousePosition != sf2MousePosition)
		{
			mbGamepadMode = false;
		}
		sf2MousePosition = rRawInput.f2MousePosition;
	}

	mMenuInput.bGamepad = mbGamepadMode;

	// Menu
	mMenuInput.flags.Set(kQuit, rRawInput.pKeyboardKeys[VK_MENU] && WasPressed(rRawInput.pKeyboardKeys[VK_F4], mPreviousRawInputMenu.pKeyboardKeys[VK_F4]));
	mMenuInput.flags.Set(kToggleFullscreen, WasPressed(rRawInput.pKeyboardKeys[VK_F1], mPreviousRawInputMenu.pKeyboardKeys[VK_F1]));
	mMenuInput.flags.Set(kMouseIsDown, rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft]);
	mMenuInput.flags.Set(kMouseClick, WasPressed(rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft], mPreviousRawInputMenu.pMouseButtons[engine::MouseButtons::kMouseButtonLeft]));
	mMenuInput.flags.Set(kGamepadButton, WasPressed(rRawInput.pGamepadButtons[engine::GamepadButtons::kGamepadButtonA], mPreviousRawInputMenu.pGamepadButtons[engine::GamepadButtons::kGamepadButtonA]));
#if defined(ENABLE_DEBUG_INPUT)
	mMenuInput.flags.Set(kQuit, WasPressed(rRawInput.pKeyboardKeys[VK_F4], mPreviousRawInputMenu.pKeyboardKeys[VK_F4]));
	mMenuInput.flags.Set(kToggleProfileText, WasPressed(rRawInput.pKeyboardKeys['P'], mPreviousRawInputMenu.pKeyboardKeys['P']));
	mMenuInput.flags.Set(kTogglePauseFrame, WasPressed(rRawInput.pKeyboardKeys[VK_SPACE], mPreviousRawInputMenu.pKeyboardKeys[VK_SPACE]));
	mMenuInput.flags.Set(kResetFrame, WasPressed(rRawInput.pKeyboardKeys[VK_RETURN], mPreviousRawInputMenu.pKeyboardKeys[VK_RETURN]));
	mMenuInput.flags.Set(kQuicksave, WasPressed(rRawInput.pKeyboardKeys[VK_F5], mPreviousRawInputMenu.pKeyboardKeys[VK_F5]));
	mMenuInput.flags.Set(kQuickload, WasPressed(rRawInput.pKeyboardKeys[VK_F6], mPreviousRawInputMenu.pKeyboardKeys[VK_F6]));
	mMenuInput.flags.Set(kSaveReplay, WasPressed(rRawInput.pKeyboardKeys[VK_F7], mPreviousRawInputMenu.pKeyboardKeys[VK_F7]));
	mMenuInput.flags.Set(kLoadReplay, WasPressed(rRawInput.pKeyboardKeys[VK_F8], mPreviousRawInputMenu.pKeyboardKeys[VK_F8]));
	mMenuInput.flags.Set(kSlowTime, WasPressed(rRawInput.pKeyboardKeys[VK_OEM_MINUS], mPreviousRawInputMenu.pKeyboardKeys[VK_OEM_MINUS]));
	mMenuInput.flags.Set(kSpeedUpTime, WasPressed(rRawInput.pKeyboardKeys[VK_OEM_PLUS], mPreviousRawInputMenu.pKeyboardKeys[VK_OEM_PLUS]));
	mMenuInput.flags.Set(kSingleStep, WasPressed(rRawInput.pKeyboardKeys[VK_TAB], mPreviousRawInputMenu.pKeyboardKeys[VK_TAB]));
	gpGame->mTimeStep.mbSingleStep = mMenuInput.flags & game::MenuInputFlags::kSingleStep;
#endif
#if defined(ENABLE_SCREENSHOTS)
	mMenuInput.flags.Set(kToggleScreenshots, WasPressed(rRawInput.pKeyboardKeys[VK_F9], mPreviousRawInputMenu.pKeyboardKeys[VK_F9]));
#endif

	mMenuInput.f2Mouse = rRawInput.f2MousePosition;
	mMenuInput.f2Gamepad = rRawInput.f2LeftThumbstick;

	// Menus
	mMenuInput.flags.Set(kPauseMenu, WasPressed(rRawInput.pKeyboardKeys[VK_ESCAPE], mPreviousRawInputMenu.pKeyboardKeys[VK_ESCAPE]) || WasPressed(rRawInput.pMouseButtons[engine::kMouseButtonMiddle], mPreviousRawInputMenu.pMouseButtons[engine::kMouseButtonMiddle]) || WasPressed(rRawInput.pGamepadButtons[engine::kGamepadMenu], mPreviousRawInputMenu.pGamepadButtons[engine::kGamepadMenu]) || WasPressed(rRawInput.pGamepadButtons[engine::kGamepadButtonB], mPreviousRawInputMenu.pGamepadButtons[engine::kGamepadButtonB]));
#if defined(ENABLE_DEBUG_INPUT)
	mMenuInput.flags.Set(kMenuGraphics, WasPressed(rRawInput.pKeyboardKeys[VK_F2], mPreviousRawInputMenu.pKeyboardKeys[VK_F2]));
	mMenuInput.flags.Set(kMenuTweaks, WasPressed(rRawInput.pKeyboardKeys[VK_F3], mPreviousRawInputMenu.pKeyboardKeys[VK_F3]));
#endif

	// Update state tracking for toggle detection
	mPreviousRawInputMenu = rRawInput;

	return mMenuInput.flags & kQuit;
}

FrameInputHeld RawInputToFrameInputHeld(const engine::RawInput& rRawInput)
{
	FrameInputHeld frameInputHeld {};

	// No frame input in main menu
	if (gpGame->InMainMenu())
	{
		return frameInputHeld;
	}

	// Gamepad
	frameInputHeld.bGamepad = gpInput->GetGamepadMode();

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
	if (rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft])
	{
		if (gpGame->meUiState == UiState::kNone)
		{
			frameInputHeld.flags |= FrameInputHeldFlags::kPrimary;
		}
	}
	else if (fGamepadMagnitude > kfGamepadThreshold)
	{
		frameInputHeld.flags |= FrameInputHeldFlags::kPrimary;
	}

	// Missile
	if (rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonRight])
	{
		frameInputHeld.flags |= FrameInputHeldFlags::kSecondary;
	}
	else if (rRawInput.f2Triggers.y > kfGamepadThreshold)
	{
		frameInputHeld.flags |= FrameInputHeldFlags::kSecondary;
	}

	// Firing direction
	auto vecMouseDirection = engine::ScreenToWorld(XMVectorSet(rRawInput.f2MousePosition.x, rRawInput.f2MousePosition.y, 0.0f, 0.0f), engine::gBaseHeight.Get()) - gpGame->CurrentFrame().interpolate.player.vecPosition;
	frameInputHeld.vecDirection = XMVector3Normalize(gpInput->GetGamepadMode() ? vecGamepadDirection : vecMouseDirection);

	if (gpInput->GetGamepadMode())
	{
		frameInputHeld.f2MovePlayer.x = 1.0f * rRawInput.f2LeftThumbstick.x;
		frameInputHeld.f2MovePlayer.y = 1.0f * rRawInput.f2LeftThumbstick.y;
	}
	else
	{
		frameInputHeld.f2MovePlayer.x = rRawInput.pKeyboardKeys['A'] ? -1.0f : (rRawInput.pKeyboardKeys['D'] ? 1.0f : 0.0f);
		frameInputHeld.f2MovePlayer.y = rRawInput.pKeyboardKeys['W'] ? 1.0f : (rRawInput.pKeyboardKeys['S'] ? -1.0f : 0.0f);

		frameInputHeld.f2MovePlayer.x += rRawInput.pKeyboardKeys[VK_LEFT] ? -1.0f : (rRawInput.pKeyboardKeys[VK_RIGHT] ? 1.0f : 0.0f);
		frameInputHeld.f2MovePlayer.y += rRawInput.pKeyboardKeys[VK_UP] ? 1.0f : (rRawInput.pKeyboardKeys[VK_DOWN] ? -1.0f : 0.0f);

		frameInputHeld.f2MovePlayer.x += rRawInput.pKeyboardKeys[VK_NUMPAD1] ? -1.0f : (rRawInput.pKeyboardKeys[VK_NUMPAD3] ? 1.0f : 0.0f);
		frameInputHeld.f2MovePlayer.y += rRawInput.pKeyboardKeys[VK_NUMPAD5] ? 1.0f : (rRawInput.pKeyboardKeys[VK_NUMPAD2] ? -1.0f : 0.0f);
	}
	frameInputHeld.f2MovePlayer.x = std::clamp(frameInputHeld.f2MovePlayer.x, -1.0f, 1.0f);
	frameInputHeld.f2MovePlayer.y = std::clamp(frameInputHeld.f2MovePlayer.y, -1.0f, 1.0f);

#if defined(ENABLE_DEBUG_INPUT)
	if (rRawInput.pKeyboardKeys[VK_OEM_6])
	{
		frameInputHeld.flags |= FrameInputHeldFlags::kZoomOut;
	}
	else if (rRawInput.pKeyboardKeys[VK_OEM_4])
	{
		frameInputHeld.flags |= FrameInputHeldFlags::kZoomIn;
	}
#endif

	return frameInputHeld;
}

FrameInputPressed Input::UpdateFrameInputPressed(const engine::RawInput& rRawInput)
{
	// No frame input in main menu
	if (gpGame->InMainMenu())
	{
		mFrameInputPressed = {};
		return mFrameInputPressed;
	}

	// Mouse wheel
	if (mScrollWheelDelay.GetDeltaNs(false) > 200'000'000ns)
	{
		mFrameInputPressed.iScrollWheel = rRawInput.iScrollWheelValue > miLastScrollWheel ? 1 : (rRawInput.iScrollWheelValue < miLastScrollWheel ? -1 : 0);
		if (mFrameInputPressed.iScrollWheel != 0)
		{
			mScrollWheelDelay.Reset();
		}
	}
	else
	{
		mFrameInputPressed.iScrollWheel = 0;
	}
	miLastScrollWheel = rRawInput.iScrollWheelValue;

	// Blaster
	mFrameInputPressed.pressedFlags.Set(FrameInputPressedFlags::kTogglePrimary, WasPressed(rRawInput.pMouseButtons[engine::kMouseButtonLeft], mPreviousRawInputFrame.pMouseButtons[engine::kMouseButtonLeft]));

	// Missile
	mFrameInputPressed.pressedFlags.Set(FrameInputPressedFlags::kToggleSecondary, WasPressed(rRawInput.pMouseButtons[engine::kMouseButtonRight], mPreviousRawInputFrame.pMouseButtons[engine::kMouseButtonRight]));

	// Skill toggle (handles multiple input sources for dash ability)
	mFrameInputPressed.pressedFlags.Set(FrameInputPressedFlags::kToggleSkill, WasPressed(rRawInput.pKeyboardKeys['E'], mPreviousRawInputFrame.pKeyboardKeys['E']) ||
																		  #if !defined(ENABLE_DEBUG_INPUT)
										                                      WasPressed(rRawInput.pKeyboardKeys[VK_SPACE], mPreviousRawInputFrame.pKeyboardKeys[VK_SPACE]) ||
																		  #endif
										                                      WasPressed(rRawInput.pKeyboardKeys[VK_NUMPAD0], mPreviousRawInputFrame.pKeyboardKeys[VK_NUMPAD0]) ||
										                                      WasPressed(rRawInput.pGamepadButtons[engine::kGamepadRightShoulder], mPreviousRawInputFrame.pGamepadButtons[engine::kGamepadRightShoulder]) ||
                                                                              (mfPreviousTriggerX < kfGamepadThreshold && rRawInput.f2Triggers.x >= kfGamepadThreshold) ||
										                                      mFrameInputPressed.iScrollWheel != 0);
	mfPreviousTriggerX = rRawInput.f2Triggers.x;

	mPreviousRawInputFrame = rRawInput;

	return mFrameInputPressed;
}

} // namespace game
