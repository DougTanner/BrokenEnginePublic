#include "Input.h"

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
	if constexpr (kbEnableProfiling)
	{
		mMenuInput.flags.Set(kToggleProfileText, KeyboardPressed('P', rRawInput));
	}
	if constexpr (kbEnableDebugInput)
	{
		mMenuInput.flags.Set(kQuit, KeyboardPressed(VK_F4, rRawInput));
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
	}
	if constexpr (kbEnableScreenshots)
	{
		mMenuInput.flags.Set(kToggleScreenshots, KeyboardPressed(VK_F9, rRawInput));
	}

	mMenuInput.f2Mouse = rRawInput.f2MousePosition;
	mMenuInput.f2Gamepad = rRawInput.f2LeftThumbstick;

	// Menus
	mMenuInput.flags.Set(kPauseMenu, KeyboardPressed(VK_ESCAPE, rRawInput) ||
						             MousePressed(engine::kMouseButtonMiddle, rRawInput) ||
						             GamepadPressed(engine::kGamepadMenu, rRawInput) ||
						             GamepadPressed(engine::kGamepadButtonB, rRawInput));
	if constexpr (kbEnableDebugInput)
	{
		mMenuInput.flags.Set(kMenuGraphics, KeyboardPressed(VK_F2, rRawInput));
		mMenuInput.flags.Set(kMenuTweaks, KeyboardPressed(VK_F3, rRawInput));
	}

	// Update state tracking for toggle detection
	mPreviousRawInputMenu = rRawInput;

	// Populate ImGui gamepad inputs when menus are visible
	if (gpGame->meUiState != UiState::kNone)
	{
		ImGuiIO& rIo = ImGui::GetIO();
		rIo.BackendFlags |= ImGuiBackendFlags_HasGamepad;

		// Map gamepad buttons to ImGui keys
		rIo.AddKeyEvent(ImGuiKey_GamepadFaceDown, rRawInput.pGamepadButtons[engine::kGamepadButtonA]);
		rIo.AddKeyEvent(ImGuiKey_GamepadFaceRight, rRawInput.pGamepadButtons[engine::kGamepadButtonB]);
		rIo.AddKeyEvent(ImGuiKey_GamepadStart, rRawInput.pGamepadButtons[engine::kGamepadMenu]);

		// D-pad stored as analog values in f2Dpad
		rIo.AddKeyEvent(ImGuiKey_GamepadDpadUp, rRawInput.f2Dpad.y > 0.5f);
		rIo.AddKeyEvent(ImGuiKey_GamepadDpadDown, rRawInput.f2Dpad.y < -0.5f);
		rIo.AddKeyEvent(ImGuiKey_GamepadDpadLeft, rRawInput.f2Dpad.x < -0.5f);
		rIo.AddKeyEvent(ImGuiKey_GamepadDpadRight, rRawInput.f2Dpad.x > 0.5f);

		// Left stick for navigation
		rIo.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, rRawInput.f2LeftThumbstick.y > 0.1f, rRawInput.f2LeftThumbstick.y);
		rIo.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, rRawInput.f2LeftThumbstick.y < -0.1f, -rRawInput.f2LeftThumbstick.y);
		rIo.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, rRawInput.f2LeftThumbstick.x < -0.1f, -rRawInput.f2LeftThumbstick.x);
		rIo.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, rRawInput.f2LeftThumbstick.x > 0.1f, rRawInput.f2LeftThumbstick.x);
	}

	return mMenuInput.flags & kQuit;
}

void RawInputToFrameInput(const engine::RawInput& rRawInput, FrameInput& rFrameInput, int64_t iHumanIndex)
{
	rFrameInput.bGamepad = gpInput->GetGamepadMode();

	// No frame input in main menu or when human is not alive
	if (gpGame->InMainMenu() || iHumanIndex < 0)
	{
		return;
	}

	if constexpr (kbEnableDebugInput)
	{
		// No frame input when ImGui wants input
		if (gpGame->mbShowImGui && (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard))
		{
			return;
		}
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

	PlayerInput& rPlayer = rFrameInput.playerInputs.at(iHumanIndex);

	// Blaster
	if (rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft])
	{
		if (gpGame->meUiState == UiState::kNone)
		{
			rPlayer.flags.Set(FrameInputHeldFlags::kPrimary);
		}
	}
	else if (fGamepadMagnitude > kfGamepadThreshold)
	{
		rPlayer.flags.Set(FrameInputHeldFlags::kPrimary);
	}

	// Missile
	if (rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonRight])
	{
		rPlayer.flags.Set(FrameInputHeldFlags::kSecondary);
	}
	else if (rRawInput.f2Triggers.y > kfGamepadThreshold)
	{
		rPlayer.flags.Set(FrameInputHeldFlags::kSecondary);
	}

	// Firing direction (camera tracks human player's world-space position)
#ifdef BT_CLIENT
	auto vecMouseDirection = gpCamera->ScreenToWorld(XMVectorSet(rRawInput.f2MousePosition.x, rRawInput.f2MousePosition.y, 0.0f, 0.0f), engine::gBaseHeight.Get()) - gpCamera->mVecPosition;
	rPlayer.vecDirection = XMVector3Normalize(gpInput->GetGamepadMode() ? vecGamepadDirection : vecMouseDirection);
#else
	rPlayer.vecDirection = XMVector3Normalize(vecGamepadDirection);
#endif

	if (gpInput->GetGamepadMode())
	{
		rPlayer.f3Move.x = 1.0f * rRawInput.f2LeftThumbstick.x;
		rPlayer.f3Move.y = 1.0f * rRawInput.f2LeftThumbstick.y;
		rPlayer.f3Move.z = 0.0f;
	}
	else
	{
		rPlayer.f3Move.x = rRawInput.pKeyboardKeys['A'] ? -1.0f : (rRawInput.pKeyboardKeys['D'] ? 1.0f : 0.0f);
		rPlayer.f3Move.y = rRawInput.pKeyboardKeys['W'] ? 1.0f : (rRawInput.pKeyboardKeys['S'] ? -1.0f : 0.0f);
		rPlayer.f3Move.z = 0.0f;

		rPlayer.f3Move.x += rRawInput.pKeyboardKeys[VK_LEFT] ? -1.0f : (rRawInput.pKeyboardKeys[VK_RIGHT] ? 1.0f : 0.0f);
		rPlayer.f3Move.y += rRawInput.pKeyboardKeys[VK_UP] ? 1.0f : (rRawInput.pKeyboardKeys[VK_DOWN] ? -1.0f : 0.0f);

		rPlayer.f3Move.x += rRawInput.pKeyboardKeys[VK_NUMPAD1] ? -1.0f : (rRawInput.pKeyboardKeys[VK_NUMPAD3] ? 1.0f : 0.0f);
		rPlayer.f3Move.y += rRawInput.pKeyboardKeys[VK_NUMPAD5] ? 1.0f : (rRawInput.pKeyboardKeys[VK_NUMPAD2] ? -1.0f : 0.0f);
	}
	rPlayer.f3Move.x = std::clamp(rPlayer.f3Move.x, -1.0f, 1.0f);
	rPlayer.f3Move.y = std::clamp(rPlayer.f3Move.y, -1.0f, 1.0f);
	rPlayer.f3Move.z = std::clamp(rPlayer.f3Move.z, -1.0f, 1.0f);

	if constexpr (kbEnableDebugInput)
	{
		if (rRawInput.pKeyboardKeys[VK_OEM_6])
		{
			rPlayer.flags.Set(FrameInputHeldFlags::kZoomOut);
		}
		else if (rRawInput.pKeyboardKeys[VK_OEM_4])
		{
			rPlayer.flags.Set(FrameInputHeldFlags::kZoomIn);
		}
	}

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
	if constexpr (!kbEnableDebugInput)
	{
		rFrameInput.pressedFlags.Set(FrameInputPressedFlags::kToggleSkill, KeyboardPressed('E', rRawInput, mPreviousRawInputFrame) ||
			KeyboardPressed(VK_SPACE, rRawInput, mPreviousRawInputFrame) ||
			KeyboardPressed(VK_NUMPAD0, rRawInput, mPreviousRawInputFrame) ||
			GamepadPressed(engine::kGamepadRightShoulder, rRawInput, mPreviousRawInputFrame) ||
			(mfPreviousTriggerX < kfGamepadThreshold && rRawInput.f2Triggers.x >= kfGamepadThreshold) ||
			rFrameInput.iScrollWheel != 0);
	}
	else
	{
		rFrameInput.pressedFlags.Set(FrameInputPressedFlags::kToggleSkill, KeyboardPressed('E', rRawInput, mPreviousRawInputFrame) ||
			KeyboardPressed(VK_NUMPAD0, rRawInput, mPreviousRawInputFrame) ||
			GamepadPressed(engine::kGamepadRightShoulder, rRawInput, mPreviousRawInputFrame) ||
			(mfPreviousTriggerX < kfGamepadThreshold && rRawInput.f2Triggers.x >= kfGamepadThreshold) ||
			rFrameInput.iScrollWheel != 0);
	}
	mfPreviousTriggerX = rRawInput.f2Triggers.x;

	mPreviousRawInputFrame = rRawInput;
}

common::crc_t FrameInput::Crc() const
{
	common::crc_t checksum = 0;
	checksum ^= common::Crc(bGamepad);
	checksum ^= common::Crc(fRotateEye);
	for (size_t i = 0; i < playerInputs.size(); ++i)
	{
		checksum ^= common::Crc(playerInputs.at(i).flags);
		checksum ^= common::Crc(playerInputs.at(i).f3Move);
		checksum ^= common::Crc(playerInputs.at(i).vecDirection);
	}
	checksum ^= common::Crc(pressedFlags);
	checksum ^= common::Crc(iScrollWheel);
	for (const StatusChange& rStatusChange : statusChanges)
	{
		checksum ^= common::Crc(rStatusChange);
	}
	return checksum;
}

std::ostream& operator<<(std::ostream& rStream, const FrameInput& rInput)
{
	common::Write(rStream, rInput.bGamepad);
	common::Write(rStream, rInput.fRotateEye);
	common::Write(rStream, rInput.pressedFlags);
	common::Write(rStream, rInput.iScrollWheel);

	int64_t iPlayerCount = static_cast<int64_t>(rInput.playerInputs.size());
	common::Write(rStream, iPlayerCount);
	if (iPlayerCount > 0)
		common::Write(rStream, rInput.playerInputs.data(), static_cast<uint64_t>(iPlayerCount));

	int64_t iStatusCount = static_cast<int64_t>(rInput.statusChanges.size());
	common::Write(rStream, iStatusCount);
	if (iStatusCount > 0)
		common::Write(rStream, rInput.statusChanges.data(), static_cast<uint64_t>(iStatusCount));

	return rStream;
}

std::istream& operator>>(std::istream& rStream, FrameInput& rInput)
{
	common::Read(rStream, rInput.bGamepad);
	common::Read(rStream, rInput.fRotateEye);
	common::Read(rStream, rInput.pressedFlags);
	common::Read(rStream, rInput.iScrollWheel);

	int64_t iPlayerCount = 0;
	common::Read(rStream, iPlayerCount);
	rInput.playerInputs.resize(iPlayerCount);
	if (iPlayerCount > 0)
		common::Read(rStream, rInput.playerInputs.data(), static_cast<uint64_t>(iPlayerCount));

	int64_t iStatusCount = 0;
	common::Read(rStream, iStatusCount);
	rInput.statusChanges.resize(iStatusCount);
	if (iStatusCount > 0)
		common::Read(rStream, rInput.statusChanges.data(), static_cast<uint64_t>(iStatusCount));

	return rStream;
}

} // namespace game
