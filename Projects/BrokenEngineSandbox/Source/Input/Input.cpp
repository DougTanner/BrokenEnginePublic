#include "Input.h"

#include "Game.h"

namespace game
{

using enum MenuInputFlags;

constexpr float kfGamepadThreshold = 0.1f;

void Input::UpdateMenuInput([[maybe_unused]] bool bLostFocus, [[maybe_unused]] MenuInput& rMenuInput)
{
#if defined(BT_CLIENT)
	engine::gpRawInputManager->Update(bLostFocus);
	const engine::RawInput& rRawInput = engine::gpRawInputManager->mRawInput;

	// Check all keyboard and mouse buttons to detect keyboard/mouse mode
	bool bKeyboardMouse = rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft] || rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonRight] || rRawInput.pKeyboardKeys['A'] || rRawInput.pKeyboardKeys['D'] || rRawInput.pKeyboardKeys['W'] || rRawInput.pKeyboardKeys['S'] || rRawInput.pKeyboardKeys[VK_LEFT] || rRawInput.pKeyboardKeys[VK_RIGHT] || rRawInput.pKeyboardKeys[VK_UP] || rRawInput.pKeyboardKeys[VK_DOWN] || rRawInput.pKeyboardKeys[VK_NUMPAD1] || rRawInput.pKeyboardKeys[VK_NUMPAD3] || rRawInput.pKeyboardKeys[VK_NUMPAD5] || rRawInput.pKeyboardKeys[VK_NUMPAD2];

	if (bKeyboardMouse)
	{
		mbGamepadMode = false;
	}
	else if (std::abs(rRawInput.f2LeftThumbstick.x) + std::abs(rRawInput.f2LeftThumbstick.y) > kfGamepadThreshold || std::abs(rRawInput.f2RightThumbstick.x) + std::abs(rRawInput.f2RightThumbstick.y) > kfGamepadThreshold)
	{
		mbGamepadMode = true;
	}
	else if (!::operator==(rRawInput.f2MousePosition, mPreviousRawInputMenu.f2MousePosition))
	{
		mbGamepadMode = false;
	}

	rMenuInput.bGamepad = mbGamepadMode;

	// Menu
	rMenuInput.flags.Set(kQuit, rRawInput.pKeyboardKeys[VK_MENU] && KeyboardPressed(VK_F4, rRawInput));
	rMenuInput.flags.Set(kToggleFullscreen, KeyboardPressed(VK_F1, rRawInput));
	rMenuInput.flags.Set(kWeaponModeToggle, KeyboardPressed('Q', rRawInput));
	rMenuInput.flags.Set(kMouseIsDown, rRawInput.pMouseButtons[engine::MouseButtons::kMouseButtonLeft]);
	rMenuInput.flags.Set(kMouseClick, MousePressed(engine::MouseButtons::kMouseButtonLeft, rRawInput));
	rMenuInput.flags.Set(kGamepadButton, GamepadPressed(engine::GamepadButtons::kGamepadButtonA, rRawInput));
	if constexpr (kbProfiling)
	{
		rMenuInput.flags.Set(kToggleProfileText, KeyboardPressed('P', rRawInput));
	}
	if constexpr (kbDebugRender)
	{
		rMenuInput.flags.Set(kToggleDebugRender, KeyboardPressed('D', rRawInput));
	}
	if constexpr (kbDebugInput)
	{
		rMenuInput.flags.Set(kQuit, KeyboardPressed(VK_F4, rRawInput));
		rMenuInput.flags.Set(kTogglePauseFrame, KeyboardPressed(VK_SPACE, rRawInput));
		rMenuInput.flags.Set(kResetFrame, KeyboardPressed(VK_RETURN, rRawInput));
		rMenuInput.flags.Set(kConnectLocal, KeyboardPressed(VK_RETURN, rRawInput));
		rMenuInput.flags.Set(kQuicksave, KeyboardPressed(VK_F5, rRawInput));
		rMenuInput.flags.Set(kQuickload, KeyboardPressed(VK_F6, rRawInput));
		rMenuInput.flags.Set(kSaveReplay, KeyboardPressed(VK_F7, rRawInput));
		rMenuInput.flags.Set(kLoadReplay, KeyboardPressed(VK_F8, rRawInput));
		rMenuInput.flags.Set(kSlowTime, KeyboardPressed(VK_OEM_MINUS, rRawInput));
		rMenuInput.flags.Set(kSpeedUpTime, KeyboardPressed(VK_OEM_PLUS, rRawInput));
		rMenuInput.flags.Set(kSingleStep, KeyboardPressed(VK_TAB, rRawInput));
	}
	if constexpr (kbScreenshots)
	{
		rMenuInput.flags.Set(kToggleScreenshots, KeyboardPressed(VK_F9, rRawInput));
	}

	rMenuInput.f2Mouse = rRawInput.f2MousePosition;
	rMenuInput.f2Gamepad = rRawInput.f2LeftThumbstick;

	// Menus
	rMenuInput.flags.Set(kPauseMenu, KeyboardPressed(VK_ESCAPE, rRawInput) || MousePressed(engine::kMouseButtonMiddle, rRawInput) || GamepadPressed(engine::kGamepadMenu, rRawInput) || GamepadPressed(engine::kGamepadButtonB, rRawInput));
	if constexpr (kbDebugInput)
	{
		rMenuInput.flags.Set(kMenuDebugTexture, KeyboardPressed(VK_F2, rRawInput));
		rMenuInput.flags.Set(kDebugTextureNext, KeyboardPressed(VK_RIGHT, rRawInput));
		rMenuInput.flags.Set(kDebugTexturePrev, KeyboardPressed(VK_LEFT, rRawInput));
		rMenuInput.flags.Set(kMenuTweaks, KeyboardPressed(VK_F3, rRawInput));
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
#endif
}

void Input::UpdateCameraInput()
{
#if defined(BT_CLIENT)
	const engine::RawInput& rRawInput = engine::gpRawInputManager->mRawInput;

	XMFLOAT2 f2Move {};
	if constexpr (kbFreeCamera)
	{
		constexpr float kfFreeCameraAxis = 0.5f;
		if (rRawInput.pKeyboardKeys['W']) { f2Move.y += kfFreeCameraAxis; }
		if (rRawInput.pKeyboardKeys['S']) { f2Move.y -= kfFreeCameraAxis; }
		if (rRawInput.pKeyboardKeys['A']) { f2Move.x -= kfFreeCameraAxis; }
		if (rRawInput.pKeyboardKeys['D']) { f2Move.x += kfFreeCameraAxis; }
	}
	mCameraInput.f2Move = f2Move;

	int iScrollNow = rRawInput.iScrollWheelValue;
	if (!mbScrollWheelInitialized)
	{
		miPreviousScrollWheelValue = iScrollNow;
		mbScrollWheelInitialized = true;
	}
	mCameraInput.iScrollDelta = iScrollNow - miPreviousScrollWheelValue;
	miPreviousScrollWheelValue = iScrollNow;
#endif
}

common::crc_t FrameInput::Crc() const
{
	common::crc_t checksum = 0;
	for (const StatusChange& rStatusChange : statusChanges)
	{
		checksum ^= common::Crc(rStatusChange.eType);
		std::visit([&](const auto& payload) { checksum ^= common::Crc(payload); }, rStatusChange.data);
	}
	return checksum;
}

std::ostream& operator<<(std::ostream& rStream, const FrameInput& rInput)
{
	int64_t iStatusCount = static_cast<int64_t>(rInput.statusChanges.size());
	common::Write(rStream, iStatusCount);
	for (const StatusChange& rChange : rInput.statusChanges)
	{
		common::Write(rStream, rChange.eType);
		std::visit([&](const auto& payload) { common::Write(rStream, payload); }, rChange.data);
	}

	return rStream;
}

std::istream& operator>>(std::istream& rStream, FrameInput& rInput)
{
	int64_t iStatusCount = 0;
	common::Read(rStream, iStatusCount);
	rInput.statusChanges.resize(iStatusCount);
	for (StatusChange& rChange : rInput.statusChanges)
	{
		common::Read(rStream, rChange.eType);
		rChange.data = DefaultDataForType(rChange.eType);
		std::visit([&](auto& payload) { common::Read(rStream, payload); }, rChange.data);
	}

	return rStream;
}

} // namespace game
