#pragma once

#if defined(BT_CLIENT)

namespace game
{
class DeathMenuScreen;
class GameSettingsScreen;
class GraphicsMenuScreen;
class HudScreen;
class MainMenuScreen;
class ModalScreen;
class PauseMenuScreen;
class SoundMenuScreen;
class TweaksScreen;
}

namespace engine
{

enum class UiTheme : uint8_t;

enum TextAreas
{
	kTextDebug,
	kTextGraphics,
	kTextProfileFps,
	kTextProfileCpuTimers,
	kTextProfileGpuTimers,
	kTextProfileCpuCounters,
	kTextProfileMemory,
	kTextProfileFrameStats,
	kTextAreasCount
};

struct TextArea
{
	static constexpr int64_t kiMaxChars = 4096;

	float fX = 0.0f;
	float fY = 0.0f;
	float fSize = 0.1f;
	int64_t iCharacterCount = 0;
	char pcText[kiMaxChars] {};
};

class ImGuiManager
{
public:

	ImGuiManager(HWND hwnd);
	~ImGuiManager();

	void Prepare(int64_t iFramebuffer);
	void Submit(int64_t iFramebuffer);
	void RegisterOpaqueRect(const ImVec2& pos, const ImVec2& size);
	void UpdateTextArea(TextAreas eTextArea, std::string_view characters);

	static constexpr int64_t kiMaxUiRects = 32;

	float mfUiScale = 1.0f;

	std::unique_ptr<game::TweaksScreen> mpTweaksScreen;

	VkBuffer mUiPrepassIndirectVkBuffer = VK_NULL_HANDLE;

private:

	void CreateRenderPass();
	void CreateFramebuffers();
	void CreateUiPrepassIndirectBuffer();
	void UpdateUiRectBuffers(int64_t iFramebuffer);
	void SetupThemeGeometry(float fUiScale);
	void ApplyThemeColors(UiTheme eTheme);
	void RenderTextAreas();

	VkRenderPass mImGuiRenderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> mImGuiFramebuffers;
	ImDrawData* mpDrawData = nullptr;
	std::unique_ptr<game::MainMenuScreen> mpMainMenuScreen;
	std::unique_ptr<game::ModalScreen> mpModalScreen;
	std::unique_ptr<game::PauseMenuScreen> mpPauseMenuScreen;
	std::unique_ptr<game::GraphicsMenuScreen> mpGraphicsMenuScreen;
	std::unique_ptr<game::SoundMenuScreen> mpSoundMenuScreen;
	std::unique_ptr<game::GameSettingsScreen> mpGameSettingsScreen;
	std::unique_ptr<game::DeathMenuScreen> mpDeathMenuScreen;
	std::unique_ptr<game::HudScreen> mpHudScreen;

	VmaAllocation mUiPrepassIndirectVmaAllocation = VK_NULL_HANDLE;
	VkDrawIndirectCommand* mpUiPrepassIndirectMapped = nullptr;

	int64_t miOpaqueRectCount = 0;
	XMFLOAT4 mOpaqueRects[kiMaxUiRects] {};

	static constexpr float kfTextEdge = 0.025f;
	TextArea mTextAreas[kTextAreasCount]
	{
		{ .fX = 0.45f, .fY = 1.0f - 2.0f * kfTextEdge },
		{ .fX = 0.875f, .fY = kfTextEdge },
		{ .fX = 0.5f * kfTextEdge, .fY = kfTextEdge },
		{ .fX = 0.5f * kfTextEdge, .fY = kfTextEdge },
		{ .fX = 0.5f * kfTextEdge, .fY = kfTextEdge },
		{ .fX = 0.875f, .fY = 0.15f },
		{ .fX = 0.725f, .fY = 0.15f },
		{ .fX = 0.5f * kfTextEdge, .fY = kfTextEdge },
	};
	static_assert(kTextAreasCount == 8);
};

inline ImGuiManager* gpImGuiManager = nullptr;

inline constexpr float kfUiReferenceHeight = 2160.0f;

// Resolution scale relative to the 2160-high reference monitor; multiply raw pixel constants by this
inline float UiScale()
{
	return gpImGuiManager != nullptr ? gpImGuiManager->mfUiScale : 1.0f;
}

} // namespace engine

#endif // defined(BT_CLIENT)
