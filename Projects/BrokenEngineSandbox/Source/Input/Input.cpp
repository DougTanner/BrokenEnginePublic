#include "Input.h"

#include "Frame/Render.h"

#include "Game.h"


namespace game
{

using enum MenuInputFlags;

constexpr float kfGamepadThreshold = 0.1f;

bool Input::UpdateMenuInput(const engine::RawInput& rRawInput)
{
	// Check all keyboard and mouse buttons to detect keyboard/mouse mode
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
		if (!::operator==(rRawInput.f2MousePosition, sf2MousePosition))
		{
			mbGamepadMode = false;
		}
		sf2MousePosition = rRawInput.f2MousePosition;
	}

	mMenuInput.bGamepad = mbGamepadMode;

	// Menu
	mMenuInput.flags.Set(kQuit, rRawInput.pKeyboardKeys[VK_MENU] && KeyboardPressed(VK_F4, rRawInput));
	mMenuInput.flags.Set(kToggleFullscreen, KeyboardPressed(VK_F1, rRawInput));
	mMenuInput.flags.Set(kMouseIsDown, rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft]);
	mMenuInput.flags.Set(kMouseClick, MousePressed(engine::MouseButtons::kMouseButtonLeft, rRawInput));
	mMenuInput.flags.Set(kGamepadButton, GamepadPressed(engine::GamepadButtons::kGamepadButtonA, rRawInput));
#if defined(ENABLE_DEBUG_INPUT)
	mMenuInput.flags.Set(kQuit, KeyboardPressed(VK_F4, rRawInput));
	mMenuInput.flags.Set(kToggleProfileText, KeyboardPressed('P', rRawInput));
	mMenuInput.flags.Set(kTogglePauseFrame, KeyboardPressed(VK_SPACE, rRawInput));
	mMenuInput.flags.Set(kResetFrame, KeyboardPressed(VK_RETURN, rRawInput));
	mMenuInput.flags.Set(kQuicksave, KeyboardPressed(VK_F5, rRawInput));
	mMenuInput.flags.Set(kQuickload, KeyboardPressed(VK_F6, rRawInput));
	mMenuInput.flags.Set(kSaveReplay, KeyboardPressed(VK_F7, rRawInput));
	mMenuInput.flags.Set(kLoadReplay, KeyboardPressed(VK_F8, rRawInput));
	mMenuInput.flags.Set(kSlowTime, KeyboardPressed(VK_OEM_MINUS, rRawInput));
	mMenuInput.flags.Set(kSpeedUpTime, KeyboardPressed(VK_OEM_PLUS, rRawInput));
	mMenuInput.flags.Set(kSingleStep, KeyboardPressed(VK_TAB, rRawInput));
	gpGame->mTimeStep.mbSingleStep = mMenuInput.flags & MenuInputFlags::kSingleStep;
#endif
#if defined(ENABLE_SCREENSHOTS)
	mMenuInput.flags.Set(kToggleScreenshots, KeyboardPressed(VK_F9, rRawInput));
#endif

	mMenuInput.f2Mouse = rRawInput.f2MousePosition;
	mMenuInput.f2Gamepad = rRawInput.f2LeftThumbstick;

	// Menus
	mMenuInput.flags.Set(kPauseMenu, KeyboardPressed(VK_ESCAPE, rRawInput) ||
						             MousePressed(engine::kMouseButtonMiddle, rRawInput) ||
						             GamepadPressed(engine::kGamepadMenu, rRawInput) ||
						             GamepadPressed(engine::kGamepadButtonB, rRawInput));
#if defined(ENABLE_DEBUG_INPUT)
	mMenuInput.flags.Set(kMenuGraphics, KeyboardPressed(VK_F2, rRawInput));
	mMenuInput.flags.Set(kMenuTweaks, KeyboardPressed(VK_F3, rRawInput));
#endif

	// Update state tracking for toggle detection
	mPreviousRawInputMenu = rRawInput;

	return mMenuInput.flags & kQuit;
}

FrameInput RawInputToFrameInput(const engine::RawInput& rRawInput)
{
	FrameInput frameInput {};

	// No frame input in main menu
	if (gpGame->InMainMenu())
	{
		return frameInput;
	}

	// Gamepad
	frameInput.bGamepad = gpInput->GetGamepadMode();

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
			frameInput.flags |= FrameInputHeldFlags::kPrimary;
		}
	}
	else if (fGamepadMagnitude > kfGamepadThreshold)
	{
		frameInput.flags |= FrameInputHeldFlags::kPrimary;
	}

	// Missile
	if (rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonRight])
	{
		frameInput.flags |= FrameInputHeldFlags::kSecondary;
	}
	else if (rRawInput.f2Triggers.y > kfGamepadThreshold)
	{
		frameInput.flags |= FrameInputHeldFlags::kSecondary;
	}

	// Firing direction
	auto vecMouseDirection = gpCamera->ScreenToWorld(XMVectorSet(rRawInput.f2MousePosition.x, rRawInput.f2MousePosition.y, 0.0f, 0.0f), engine::gBaseHeight.Get()) - gpGame->CurrentFrame().interpolate.player.vecPosition;
	frameInput.vecDirection = XMVector3Normalize(gpInput->GetGamepadMode() ? vecGamepadDirection : vecMouseDirection);

	if (gpInput->GetGamepadMode())
	{
		frameInput.f3MovePlayer.x = 1.0f * rRawInput.f2LeftThumbstick.x;
		frameInput.f3MovePlayer.y = 1.0f * rRawInput.f2LeftThumbstick.y;
		frameInput.f3MovePlayer.z = 0.0f;
	}
	else
	{
		frameInput.f3MovePlayer.x = rRawInput.pKeyboardKeys['A'] ? -1.0f : (rRawInput.pKeyboardKeys['D'] ? 1.0f : 0.0f);
		frameInput.f3MovePlayer.y = rRawInput.pKeyboardKeys['W'] ? 1.0f : (rRawInput.pKeyboardKeys['S'] ? -1.0f : 0.0f);
		frameInput.f3MovePlayer.z = 0.0f;

		frameInput.f3MovePlayer.x += rRawInput.pKeyboardKeys[VK_LEFT] ? -1.0f : (rRawInput.pKeyboardKeys[VK_RIGHT] ? 1.0f : 0.0f);
		frameInput.f3MovePlayer.y += rRawInput.pKeyboardKeys[VK_UP] ? 1.0f : (rRawInput.pKeyboardKeys[VK_DOWN] ? -1.0f : 0.0f);

		frameInput.f3MovePlayer.x += rRawInput.pKeyboardKeys[VK_NUMPAD1] ? -1.0f : (rRawInput.pKeyboardKeys[VK_NUMPAD3] ? 1.0f : 0.0f);
		frameInput.f3MovePlayer.y += rRawInput.pKeyboardKeys[VK_NUMPAD5] ? 1.0f : (rRawInput.pKeyboardKeys[VK_NUMPAD2] ? -1.0f : 0.0f);
	}
	frameInput.f3MovePlayer.x = std::clamp(frameInput.f3MovePlayer.x, -1.0f, 1.0f);
	frameInput.f3MovePlayer.y = std::clamp(frameInput.f3MovePlayer.y, -1.0f, 1.0f);
	frameInput.f3MovePlayer.z = std::clamp(frameInput.f3MovePlayer.z, -1.0f, 1.0f);

#if defined(ENABLE_DEBUG_INPUT)
	if (rRawInput.pKeyboardKeys[VK_OEM_6])
	{
		frameInput.flags |= FrameInputHeldFlags::kZoomOut;
	}
	else if (rRawInput.pKeyboardKeys[VK_OEM_4])
	{
		frameInput.flags |= FrameInputHeldFlags::kZoomIn;
	}
#endif

	return frameInput;
}

void Input::UpdateFrameInputPressed(const engine::RawInput& rRawInput, FrameInput& rFrameInput)
{
	// No frame input in main menu
	if (gpGame->InMainMenu())
	{
		rFrameInput.ClearPressed();
		return;
	}

	// Mouse wheel
	if (mScrollWheelDelay.GetDeltaNs(false) > 200'000'000ns)
	{
		rFrameInput.iScrollWheel = rRawInput.iScrollWheelValue > miLastScrollWheel ? 1 : (rRawInput.iScrollWheelValue < miLastScrollWheel ? -1 : 0);
		if (rFrameInput.iScrollWheel != 0)
		{
			mScrollWheelDelay.Reset();
		}
	}
	else
	{
		rFrameInput.iScrollWheel = 0;
	}
	miLastScrollWheel = rRawInput.iScrollWheelValue;

	// Blaster
	rFrameInput.pressedFlags.Set(FrameInputPressedFlags::kTogglePrimary, MousePressed(engine::kMouseButtonLeft, rRawInput, mPreviousRawInputFrame));

	// Missile
	rFrameInput.pressedFlags.Set(FrameInputPressedFlags::kToggleSecondary, MousePressed(engine::kMouseButtonRight, rRawInput, mPreviousRawInputFrame));

	// Skill toggle (handles multiple input sources for dash ability)
	rFrameInput.pressedFlags.Set(FrameInputPressedFlags::kToggleSkill, KeyboardPressed('E', rRawInput, mPreviousRawInputFrame) ||
                                                                        #if !defined(ENABLE_DEBUG_INPUT)
	                                                                        KeyboardPressed(VK_SPACE, rRawInput, mPreviousRawInputFrame) ||
                                                                        #endif
	                                                                        KeyboardPressed(VK_NUMPAD0, rRawInput, mPreviousRawInputFrame) ||
                                                                            GamepadPressed(engine::kGamepadRightShoulder, rRawInput, mPreviousRawInputFrame) ||
										                                    (mfPreviousTriggerX < kfGamepadThreshold && rRawInput.f2Triggers.x >= kfGamepadThreshold) ||
										                                    rFrameInput.iScrollWheel != 0);
	mfPreviousTriggerX = rRawInput.f2Triggers.x;

	mPreviousRawInputFrame = rRawInput;
}

} // namespace game
