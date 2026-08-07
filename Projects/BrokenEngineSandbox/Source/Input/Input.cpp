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
	bool bKeyboardMouse = (rRawInput.mouseButtons & engine::MouseButtons::kMouseButtonLeft) || (rRawInput.mouseButtons & engine::MouseButtons::kMouseButtonRight) || rRawInput.pKeyboardKeys['A'] || rRawInput.pKeyboardKeys['D'] || rRawInput.pKeyboardKeys['W'] || rRawInput.pKeyboardKeys['S'] || rRawInput.pKeyboardKeys[VK_LEFT] || rRawInput.pKeyboardKeys[VK_RIGHT] || rRawInput.pKeyboardKeys[VK_UP] || rRawInput.pKeyboardKeys[VK_DOWN] || rRawInput.pKeyboardKeys[VK_NUMPAD1] || rRawInput.pKeyboardKeys[VK_NUMPAD3] || rRawInput.pKeyboardKeys[VK_NUMPAD5] || rRawInput.pKeyboardKeys[VK_NUMPAD2];

	if (bKeyboardMouse)
	{
		mStateFlags.Clear(InputStateFlags::kGamepadMode);
	}
	else if (std::abs(rRawInput.f2LeftThumbstick.x) + std::abs(rRawInput.f2LeftThumbstick.y) > kfGamepadThreshold || std::abs(rRawInput.f2RightThumbstick.x) + std::abs(rRawInput.f2RightThumbstick.y) > kfGamepadThreshold)
	{
		mStateFlags.Set(InputStateFlags::kGamepadMode);
	}
	else if (!::operator==(rRawInput.f2MousePosition, mPreviousRawInputMenu.f2MousePosition))
	{
		mStateFlags.Clear(InputStateFlags::kGamepadMode);
	}

	rMenuInput.bGamepad = mStateFlags & InputStateFlags::kGamepadMode;

	// Menu
	rMenuInput.flags.Set(kQuit, rRawInput.pKeyboardKeys[VK_MENU] && KeyboardPressed(VK_F4, rRawInput));
	rMenuInput.flags.Set(kToggleFullscreen, KeyboardPressed(VK_F1, rRawInput));
	rMenuInput.flags.Set(kMouseIsDown, rRawInput.mouseButtons & engine::MouseButtons::kMouseButtonLeft);
	rMenuInput.flags.Set(kMouseClick, MousePressed(engine::MouseButtons::kMouseButtonLeft, rRawInput));
	rMenuInput.flags.Set(kGamepadButton, GamepadPressed(engine::GamepadButtons::kGamepadButtonA, rRawInput));
	if constexpr (kbProfiling)
	{
		rMenuInput.flags.Set(kToggleProfileText, KeyboardPressed('P', rRawInput));
	}
	if constexpr (kbDebugRender)
	{
		rMenuInput.flags.Set(kToggleDebugRender, KeyboardPressed('Q', rRawInput));
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
		rMenuInput.flags.Set(kCycleMenuIsland, KeyboardPressed('E', rRawInput));
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

	// Populate ImGui gamepad inputs when menus are visible. Guard GetIO(): the ImGui context can be destroyed across a
	// multi-frame deferred swapchain recreate (minimized client), and this fires every deferred frame if a menu was open.
	if (gpGame->meUiState != UiState::kNone && ImGui::GetCurrentContext() != nullptr)
	{
		ImGuiIO& rIo = ImGui::GetIO();
		rIo.BackendFlags |= ImGuiBackendFlags_HasGamepad;

		// Map gamepad buttons to ImGui keys
		rIo.AddKeyEvent(ImGuiKey_GamepadFaceDown, rRawInput.gamepadButtons & engine::kGamepadButtonA);
		rIo.AddKeyEvent(ImGuiKey_GamepadFaceRight, rRawInput.gamepadButtons & engine::kGamepadButtonB);
		rIo.AddKeyEvent(ImGuiKey_GamepadStart, rRawInput.gamepadButtons & engine::kGamepadMenu);

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
	if (!(mStateFlags & InputStateFlags::kScrollWheelInitialized))
	{
		miPreviousScrollWheelValue = iScrollNow;
		mStateFlags.Set(InputStateFlags::kScrollWheelInitialized);
	}
	// The UI owns the wheel only while the cursor is over a window that can actually scroll; a non-scrolling panel,
	// modal, or empty background leaves the notch with the camera, menus open or not. This mirrors the routing
	// ImGui::UpdateMouseWheel performs, so a notch is never both scrolled and zoomed: for a root window its scroll
	// target is the hovered window (its bubble loop only walks child windows, and this UI creates none), and
	// WheelingWindow covers the lock during which ImGui keeps feeding an earlier target regardless of current hover.
	// Hover is one frame old — ImGui resolves it in NewFrame, after this poll — so a notch arriving on the frame the
	// cursor crosses a panel edge is judged against the previous hover.
	const ImGuiContext* pImGuiContext = ImGui::GetCurrentContext();
	const ImGuiWindow* pHoveredWindow = pImGuiContext != nullptr ? pImGuiContext->HoveredWindow : nullptr;
	bool bUserInterfaceOwnsScroll = (pImGuiContext != nullptr && pImGuiContext->WheelingWindow != nullptr) ||
		(pHoveredWindow != nullptr && pHoveredWindow->ScrollMax.y != 0.0f && !(pHoveredWindow->Flags & (ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMouseInputs)));
	mCameraInput.iScrollDelta = bUserInterfaceOwnsScroll ? 0 : iScrollNow - miPreviousScrollWheelValue;
	miPreviousScrollWheelValue = iScrollNow;
#endif
}

// Same-session-only CRC: feeds the replay DifferenceStream writer-side dedup and a verbose reader log,
// never persisted or compared cross-build. Unlike the frame CRC it needs no game::Frame::kiVersion bump
// when the mixing algorithm changes.
common::crc_t FrameInput::Crc() const
{
	common::crc_t checksum = 0;
	for (const StatusChange& rStatusChange : statusChanges)
	{
		checksum = (checksum ^ common::Crc(rStatusChange.eType)) * common::kCrcMultiplier;
		std::visit([&](const auto& payload) { checksum = (checksum ^ common::Crc(payload)) * common::kCrcMultiplier; }, rStatusChange.data);
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
	// Trust boundary (replay stream): bound the count against the stream before the resize — each
	// StatusChange serializes at least a type byte plus one payload byte.
	common::ValidateDeserializedCount(iStatusCount, sizeof(uint8_t) + 1, rStream, "FrameInput statusChanges");
	rInput.statusChanges.resize(iStatusCount);
	for (StatusChange& rChange : rInput.statusChanges)
	{
		common::Read(rStream, rChange.eType);
		// Trust boundary (replay stream): an unknown type tag seats the default variant alternative and reads
		// the wrong payload byte count, silently desyncing the rest of the stream — reject it.
		if (!IsKnownStatusChangeType(rChange.eType))
		{
			throw common::CorruptStreamException("FrameInput StatusChange type");
		}
		rChange.data = DefaultDataForType(rChange.eType);
		std::visit([&](auto& payload) { common::Read(rStream, payload); }, rChange.data);
	}

	return rStream;
}

} // namespace game
