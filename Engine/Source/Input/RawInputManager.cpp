#include "RawInputManager.h"

#if defined(BT_CLIENT)

#include "Game.h"

namespace engine
{

RawInputManager::RawInputManager()
{
	ASSERT(gpRawInputManager == nullptr);

	gpRawInputManager = this;

	try
	{
		mpGamePad = std::make_unique<GamePad>();
	}
	catch ([[maybe_unused]] const std::exception& rException)
	{
		LOG(kInput, kError, "Failed GamePad: {}", rException.what());
	}
	catch (...)
	{
		LOG(kInput, kError, "Failed GamePad");
	}
}

RawInputManager::~RawInputManager()
{
	if (gpRawInputManager == this)
	{
		gpRawInputManager = nullptr;
	}
}

void RawInputManager::UpdateFocus(bool bHasFocus, HWND hwnd)
{
	mStateFlags.Set(RawInputStateFlags::kHasFocus, bHasFocus);
	mHwnd = hwnd;

	// Keyboard HID (usage page 0x01, usage 0x06). Register and unregister share the same device struct and
	// differ only in dwFlags: RIDEV_NOLEGACY adds the HID keyboard and ignores legacy keyboard messages;
	// RIDEV_REMOVE unregisters it.
	RAWINPUTDEVICE rawinputdevice
	{
		.usUsagePage = 0x01,
		.usUsage = 0x06,
		.dwFlags = static_cast<DWORD>(bHasFocus ? RIDEV_NOLEGACY : RIDEV_REMOVE),
		.hwndTarget = nullptr,
	};
	if (bHasFocus)
	{
		LOG(kInput, kInfo, "RegisterRawInputDevices");
		if (RegisterRawInputDevices(&rawinputdevice, 1, sizeof(RAWINPUTDEVICE)) == FALSE)
		{
			// Heap: common::LastErrorString() returns a std::string by value (exceeds SSO), and this LOG sits in the allocation-tracked main loop
			ScopedSuppressAllocationTracking suppress;
			LOG(kInput, kError, "Failed to register raw input: {}", common::LastErrorString().data());
		}

		if (mpGamePad != nullptr)
		{
			mpGamePad->Resume();
		}

		std::fill(std::begin(mpbKeyboardKeysDown), std::end(mpbKeyboardKeysDown), false);
	}
	else
	{
		TrapCursor(false);

		LOG(kInput, kInfo, "UnregisterRawInputDevices");
		if (RegisterRawInputDevices(&rawinputdevice, 1, sizeof(RAWINPUTDEVICE)) == FALSE)
		{
			// Heap: common::LastErrorString() returns a std::string by value (exceeds SSO), and this LOG sits in the allocation-tracked main loop
			ScopedSuppressAllocationTracking suppress;
			LOG(kInput, kError, "Failed to unregister raw input: {}", common::LastErrorString().data());
		}

		if (mpGamePad != nullptr)
		{
			mpGamePad->Suspend();
		}
	}
}

bool RawInputManager::SetVibration(int64_t iPlayer, float fLeftMotor, float fRightMotor, float fLeftTrigger, float fRightTrigger)
{
	if (mpGamePad != nullptr)
	{
		return mpGamePad->SetVibration(static_cast<int>(iPlayer), fLeftMotor, fRightMotor, fLeftTrigger, fRightTrigger);
	}

	return false;
}

void RawInputManager::TrapCursor(bool bTrap)
{
	if (bTrap)
	{
		RECT rect {};
		GetClientRect(mHwnd, &rect);
		POINT pointUpperLeft {};
		pointUpperLeft.x = rect.left;
		pointUpperLeft.y = rect.top;
		POINT pointLowerRight {};
		pointLowerRight.x = rect.right;
		pointLowerRight.y = rect.bottom;
		MapWindowPoints(mHwnd, nullptr, &pointUpperLeft, 1);
		MapWindowPoints(mHwnd, nullptr, &pointLowerRight, 1);
		rect.left = pointUpperLeft.x;
		rect.top = pointUpperLeft.y;
		rect.right = pointLowerRight.x;
		rect.bottom = pointLowerRight.y;
		ClipCursor(&rect);
	}
	else
	{
		ClipCursor(nullptr);
	}
}

void RawInputManager::Update(bool bLostFocus)
{
	if (bLostFocus) [[unlikely]]
	{
		TrapCursor(false);
	}
	else
	{
		// Suppressed harness client forces the trap off regardless of game setting so a physical cursor is never clipped to the window.
		TrapCursor(PhysicalInputSuppressed() ? false : game::gpGame->ShouldTrapCursor());
	}

	// A running agent input script relaxes the unfocused early-out so it can publish + overlay while the window is
	// unfocused (the norm for the harness). Off the script path this stays the plain focus gate.
	bool bScriptActive = gpAgentInput != nullptr && gpAgentInput->IsScriptActive();
	bool bHasFocus = mStateFlags & RawInputStateFlags::kHasFocus;
	if (!bHasFocus && !bScriptActive)
	{
		return;
	}

	// Keyboard
	if (bHasFocus)
	{
		for (int64_t i = 0; i < kiKeyboardKeyCount; ++i)
		{
			mRawInput.pKeyboardKeys[i] = mpbKeyboardKeysDown[i];
		}
	}
	else
	{
		// Relaxed unfocused-publish path: the hardware keyboard is unregistered (RIDEV_REMOVE) while unfocused, so
		// mpbKeyboardKeysDown is frozen at its last focused state — zero the published keyboard so the overlay ORs
		// synthetic keys onto a clean snapshot instead of republishing stuck key bits for the whole script.
		std::fill(std::begin(mRawInput.pKeyboardKeys), std::end(mRawInput.pKeyboardKeys), false);
	}

	// Mouse
	Mouse::State mouseState = mMouse.GetState();
	mRawInput.f2MousePosition.x = static_cast<float>(mouseState.x) / static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	mRawInput.f2MousePosition.y = static_cast<float>(mouseState.y) / static_cast<float>(gpGraphics->mFramebufferExtent2D.height);
	mRawInput.mouseButtons.Set(MouseButtons::kMouseButtonLeft, mouseState.leftButton);
	mRawInput.mouseButtons.Set(MouseButtons::kMouseButtonMiddle, mouseState.middleButton);
	mRawInput.mouseButtons.Set(MouseButtons::kMouseButtonRight, mouseState.rightButton);
	mRawInput.mouseButtons.Set(MouseButtons::kMouseButtonExtraOne, mouseState.xButton1);
	mRawInput.mouseButtons.Set(MouseButtons::kMouseButtonExtraTwo, mouseState.xButton2);
	mRawInput.iScrollWheelValue = mouseState.scrollWheelValue;

	// iScrollWheelValue is a lifetime accumulator consumers diff (game Input.cpp camera zoom). The agent's persistent
	// synthetic scroll offset must participate in EVERY publish — not only script-active frames — or the offset
	// vanishing when a script ends (or on refocus) would look like an equal-and-opposite phantom zoom. Null-guarded:
	// server build and the agent-disabled client path have no AgentInput. (Single owner of the addition; the Overlay
	// sink deliberately does not re-add it.)
	if (gpAgentInput != nullptr)
	{
		mRawInput.iScrollWheelValue += gpAgentInput->SyntheticScrollAccumulator();
	}

	// Game pad (only first game pad supported). Suppressed harness client skips the poll — the snapshot stays zero-initialized.
	if (mpGamePad != nullptr && !PhysicalInputSuppressed())
	{
		GamePad::State gamepadState = mpGamePad->GetState(0);
		if (gamepadState.IsConnected())
		{
			if (!(mStateFlags & RawInputStateFlags::kGamePadConnected))
			{
				mStateFlags.Set(RawInputStateFlags::kGamePadConnected);
				LOG(kInput, kInfo, "Game pad connected: {}", static_cast<int64_t>(mpGamePad->GetCapabilities(0).gamepadType));
			}

			mRawInput.f2LeftThumbstick.x = gamepadState.thumbSticks.leftX;
			mRawInput.f2LeftThumbstick.y = gamepadState.thumbSticks.leftY;
			mRawInput.f2RightThumbstick.x = gamepadState.thumbSticks.rightX;
			mRawInput.f2RightThumbstick.y = gamepadState.thumbSticks.rightY;

			mRawInput.f2Dpad.x = gamepadState.dpad.left ? -1.0f : (gamepadState.dpad.right ? 1.0f : 0.0f);
			mRawInput.f2Dpad.y = gamepadState.dpad.up ? 1.0f : (gamepadState.dpad.down ? -1.0f : 0.0f);

			mRawInput.gamepadButtons.Set(kGamepadButtonA, gamepadState.IsAPressed());
			mRawInput.gamepadButtons.Set(kGamepadButtonB, gamepadState.IsBPressed());
			mRawInput.gamepadButtons.Set(kGamepadButtonX, gamepadState.IsXPressed());
			mRawInput.gamepadButtons.Set(kGamepadButtonY, gamepadState.IsYPressed());
			mRawInput.gamepadButtons.Set(kGamepadLeftShoulder, gamepadState.IsLeftShoulderPressed());
			mRawInput.gamepadButtons.Set(kGamepadRightShoulder, gamepadState.IsRightShoulderPressed());
			mRawInput.gamepadButtons.Set(kGamepadStart, gamepadState.IsStartPressed());
			mRawInput.gamepadButtons.Set(kGamepadMenu, gamepadState.IsMenuPressed());
		}
		else
		{
			if (mStateFlags & RawInputStateFlags::kGamePadConnected)
			{
				mStateFlags.Clear(RawInputStateFlags::kGamePadConnected);
				LOG(kInput, kInfo, "Game pad disconnected");
			}

			mRawInput.f2LeftThumbstick.x = 0.0f;
			mRawInput.f2LeftThumbstick.y = 0.0f;
			mRawInput.f2RightThumbstick.x = 0.0f;
			mRawInput.f2RightThumbstick.y = 0.0f;

			mRawInput.f2Dpad.x = 0.0f;
			mRawInput.f2Dpad.y = 0.0f;

			mRawInput.gamepadButtons = {};
		}
	}

	// Agent synthetic-input overlay: OR script-driven keys / mouse buttons / mouse pos / scroll onto the just-
	// published snapshot. Must run AFTER the keyboard copy loop so synthetic key bits are not overwritten by it.
	if (bScriptActive)
	{
		gpAgentInput->Overlay(mRawInput);
	}
}

void RawInputManager::HandleRawInput(LPARAM lparam)
{
	HRAWINPUT hrawinput = reinterpret_cast<HRAWINPUT>(lparam);

	// Only the fixed-size keyboard usage is registered (UpdateFocus), never variable-length RAWHID, so sizeof(RAWINPUT) bounds every packet
	RAWINPUT rawinput {};
	UINT uiRawInputBytes = sizeof(rawinput);
	if (GetRawInputData(hrawinput, RID_INPUT, &rawinput, &uiRawInputBytes, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
	{
		// Heap: common::LastErrorString() returns a std::string by value (exceeds SSO), and this LOG sits in the allocation-tracked main loop
		ScopedSuppressAllocationTracking suppress;
		LOG(kInput, kWarning, "GetRawInputData failed: {}", common::LastErrorString().data());
		DEBUG_BREAK();
		return;
	}

	if (rawinput.header.dwType == RIM_TYPEKEYBOARD && !PhysicalInputSuppressed())
	{
		USHORT uiKey = rawinput.data.keyboard.VKey;
		if (uiKey < kiKeyboardKeyCount)
		{
			mpbKeyboardKeysDown[uiKey] = (rawinput.data.keyboard.Flags & RI_KEY_BREAK) == 0;
		}
	}
}

} // namespace engine

#endif
