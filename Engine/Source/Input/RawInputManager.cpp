#include "RawInputManager.h"

#include "Graphics/Graphics.h"

#include "Game.h"

namespace engine
{

RawInputManager::RawInputManager()
{
	gpRawInputManager = this;

	try
	{
		LOG("DirectX::GamePad");
		mpGamePad = std::make_unique<DirectX::GamePad>();
	}
	catch ([[maybe_unused]] std::exception& rException)
	{
		LOG("Failed DirectX::GamePad: {}", rException.what());
	}
	catch (...)
	{
		LOG("Failed DirectX::GamePad");
	}
}

RawInputManager::~RawInputManager()
{
	gpRawInputManager = nullptr;
}

void RawInputManager::UpdateFocus(bool bHasFocus, HWND hwnd)
{
	mbHasFocus = bHasFocus;

	RAWINPUTDEVICE pRawinputdevices[2] {};
	if (bHasFocus)
	{
		pRawinputdevices[0].usUsagePage = 0x01;
		pRawinputdevices[0].usUsage = 0x02;
		pRawinputdevices[0].dwFlags = RIDEV_INPUTSINK; // adds HID mouse and also ignores legacy mouse messages
		pRawinputdevices[0].hwndTarget = hwnd;

		pRawinputdevices[1].usUsagePage = 0x01;
		pRawinputdevices[1].usUsage = 0x06;
		pRawinputdevices[1].dwFlags = RIDEV_NOLEGACY; // adds HID keyboard and also ignores legacy keyboard messages
		pRawinputdevices[1].hwndTarget = nullptr;

		LOG("RegisterRawInputDevices");
		if (RegisterRawInputDevices(pRawinputdevices, 2, sizeof(RAWINPUTDEVICE)) == FALSE)
		{
			LOG("Failed to register raw input: {}", common::LastErrorString().data());
		}
		
		if (mpGamePad != nullptr)
		{
			mpGamePad->Resume();
		}

		memset(mpbKeyboardKeysDown, 0, sizeof(mpbKeyboardKeysDown));
	}
	else
	{
		TrapCursor(false);

		pRawinputdevices[0].usUsagePage = 0x01;
		pRawinputdevices[0].usUsage = 0x02;
		pRawinputdevices[0].dwFlags = RIDEV_REMOVE;
		pRawinputdevices[0].hwndTarget = nullptr;

		pRawinputdevices[1].usUsagePage = 0x01;
		pRawinputdevices[1].usUsage = 0x06;
		pRawinputdevices[1].dwFlags = RIDEV_REMOVE;
		pRawinputdevices[1].hwndTarget = nullptr;

		LOG("UnregisterRawInputDevices");
		if (RegisterRawInputDevices(pRawinputdevices, 2, sizeof(RAWINPUTDEVICE)) == FALSE)
		{
			LOG("Failed to unregister raw input: {}", common::LastErrorString().data());
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

const RawInput& RawInputManager::Update()
{
	if (!mbHasFocus)
	{
		return mRawInput;
	}

	// Keyboard
	for (int64_t i = 0; i < kiKeyboardKeyCount; ++i)
	{
		mRawInput.pKeyboardKeys[i].UpdateToggle(mpbKeyboardKeysDown[i]);
	}

	// Mouse
	DirectX::Mouse::State mouseState = mMouse.GetState();
	mRawInput.pMouseButtons[MouseButtons::kMouseButtonLeft].UpdateToggle(mouseState.leftButton);
	mRawInput.pMouseButtons[MouseButtons::kMouseButtonMiddle].UpdateToggle(mouseState.middleButton);
	mRawInput.pMouseButtons[MouseButtons::kMouseButtonRight].UpdateToggle(mouseState.rightButton);
	mRawInput.pMouseButtons[MouseButtons::kMouseButtonExtraOne].UpdateToggle(mouseState.xButton1);
	mRawInput.pMouseButtons[MouseButtons::kMouseButtonExtraTwo].UpdateToggle(mouseState.xButton2);
	mRawInput.f2MousePosition.x = static_cast<float>(mouseState.x) / static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	mRawInput.f2MousePosition.y = static_cast<float>(mouseState.y) / static_cast<float>(gpGraphics->mFramebufferExtent2D.height);


	static common::Timer sScrollWheelDelay;
	static int siLastScrollWheel = 0;
	if (sScrollWheelDelay.GetDeltaNs(false) > 200'000'000ns)
	{
		mRawInput.iScrollWheel = mouseState.scrollWheelValue > siLastScrollWheel ? 1 : (mouseState.scrollWheelValue < siLastScrollWheel ? -1 : 0);
		if (mRawInput.iScrollWheel != 0)
		{
			sScrollWheelDelay.Reset();
		}
	}
	else
	{
		mRawInput.iScrollWheel = 0;
	}
	siLastScrollWheel = mouseState.scrollWheelValue;

	// Game pad (only first game pad supported)
	if (mpGamePad != nullptr)
	{
		DirectX::GamePad::State gamepadState = mpGamePad->GetState(0);
		if (gamepadState.IsConnected())
		{
			if (!mbGamePadConnected)
			{
				DirectX::GamePad::Capabilities gamepadCapabilities = mpGamePad->GetCapabilities(0);
				mbGamePadConnected = true;
				LOG("Game pad connected: {}", static_cast<int64_t>(gamepadCapabilities.gamepadType));
			}

			mRawInput.pGamepadButtons[kGamepadButtonA].UpdateToggle(gamepadState.IsAPressed());
			mRawInput.pGamepadButtons[kGamepadButtonB].UpdateToggle(gamepadState.IsBPressed());
			mRawInput.pGamepadButtons[kGamepadButtonX].UpdateToggle(gamepadState.IsXPressed());
			mRawInput.pGamepadButtons[kGamepadButtonY].UpdateToggle(gamepadState.IsYPressed());
			mRawInput.pGamepadButtons[kGamepadLeftShoulder].UpdateToggle(gamepadState.IsLeftShoulderPressed());
			mRawInput.pGamepadButtons[kGamepadRightShoulder].UpdateToggle(gamepadState.IsRightShoulderPressed());
			mRawInput.pGamepadButtons[kGamepadStart].UpdateToggle(gamepadState.IsStartPressed());
			mRawInput.pGamepadButtons[kGamepadMenu].UpdateToggle(gamepadState.IsMenuPressed());

			mRawInput.f2LeftThumbstick.x = gamepadState.thumbSticks.leftX;
			mRawInput.f2LeftThumbstick.y = gamepadState.thumbSticks.leftY;
			mRawInput.f2RightThumbstick.x = gamepadState.thumbSticks.rightX;
			mRawInput.f2RightThumbstick.y = gamepadState.thumbSticks.rightY;

			mRawInput.f2Dpad.x = gamepadState.dpad.left ? -1.0f : (gamepadState.dpad.right ? 1.0f : 0.0f);
			mRawInput.f2Dpad.y = gamepadState.dpad.up ? 1.0f : (gamepadState.dpad.down ? -1.0f : 0.0f);

			mRawInput.f2Triggers.x = gamepadState.triggers.left;
			mRawInput.f2Triggers.y = gamepadState.triggers.right;
		}
		else
		{
			if (mbGamePadConnected)
			{
				mbGamePadConnected = false;
				LOG("Game pad disconnected");
			}

			mRawInput.pGamepadButtons[kGamepadButtonA].UpdateToggle(false);
			mRawInput.pGamepadButtons[kGamepadButtonB].UpdateToggle(false);
			mRawInput.pGamepadButtons[kGamepadButtonX].UpdateToggle(false);
			mRawInput.pGamepadButtons[kGamepadButtonY].UpdateToggle(false);

			mRawInput.f2LeftThumbstick.x = 0.0f;
			mRawInput.f2LeftThumbstick.y = 0.0f;
			mRawInput.f2RightThumbstick.x = 0.0f;
			mRawInput.f2RightThumbstick.y = 0.0f;
		}
	}

	return mRawInput;
}

void RawInputManager::HandleRawInput(LPARAM lparam)
{
	auto hrawinput = reinterpret_cast<HRAWINPUT>(lparam);

	UINT uiRawInputBytes = 0;
	GetRawInputData(hrawinput, RID_INPUT, nullptr, &uiRawInputBytes, sizeof(RAWINPUTHEADER));

	auto pRawinput = common::gpThreadLocal->GetWorkbuffer<RAWINPUT*>(uiRawInputBytes);
	if (GetRawInputData(hrawinput, RID_INPUT, pRawinput, &uiRawInputBytes, sizeof(RAWINPUTHEADER)) != uiRawInputBytes)
	{
		LOG("GetRawInputData did not return correct size!");
		DEBUG_BREAK();
		return;
	}

	if (pRawinput->header.dwType == RIM_TYPEKEYBOARD)
	{
		USHORT key = pRawinput->data.keyboard.VKey;
		if (key < kiKeyboardKeyCount)
		{
			mpbKeyboardKeysDown[key] = (pRawinput->data.keyboard.Flags & RI_KEY_BREAK) == 0;
		}
	}
}

} // namespace engine
