#pragma once

#if defined(BT_CLIENT)

namespace engine
{

struct RawInput;

// Which synthetic-input script is running. Each agent input command (click / hover / set_slider / key / mouse)
// compiles to one of these; only one runs at a time.
enum class AgentScriptKind : uint8_t
{
	kClick,
	kHover,
	kSetSlider,
	kKey,
	kMouse,
};

// mouse command sub-action.
enum class AgentMouseAction : uint8_t
{
	kMove,
	kDown,
	kUp,
	kClick,
	kWheel,
};

// Completion status polled by the deferred command response.
enum class AgentScriptStatus : uint8_t
{
	kPending,
	kDone,
	kTimeout,
	kNotFound,
	kAmbiguous,
	kNotInputable, // set_slider target resolved but lacks ImGuiItemStatusFlags_Inputable (e.g. a Button)
};

// Immutable configuration a command hands to BeginScript(). Fixed-size (no heap); string params are bounded buffers.
struct AgentScript
{
	AgentScriptKind eKind = AgentScriptKind::kClick;

	char pcLabel[64] {};   // click / hover / set_slider target
	char pcWindow[32] {};  // optional window filter
	bool bHasWindow = false;

	char pcValueText[32] {}; // set_slider value text (ASCII, typed via ImGui temp-input)

	int32_t iTimeoutFrames = 120; // stabilization timeout (click / hover / set_slider)
	int32_t iHoldFrames = 2;      // hover / key hold duration

	int32_t iKeyVk = 0; // key command: Win32 VK code driven through the RawInput overlay

	AgentMouseAction eMouseAction = AgentMouseAction::kMove;
	int32_t iImGuiMouseButton = 0; // 0 left / 1 right / 2 middle (ImGui IO button index)
	uint32_t uiOverlayMouseButtonBit = 0; // engine::MouseButtons bit for the RawInput overlay
	int32_t iWheelNotches = 0;
	float f2CoordPixels[2] {}; // mouse command raw pixel coords
	bool bHasCoord = false;
};

// Frame-stepped synthetic-input engine. Advanced one step per rendered frame at the client drain point
// (before ImGui NewFrame), so injected events land in the same frame. Two sinks: ImGui IO events (UI driving) and
// a RawInput snapshot overlay (game key bindings). Zero steady-state heap — all state is fixed members.
class AgentInput
{
public:

	AgentInput();
	~AgentInput();

	AgentInput(const AgentInput&) = delete;
	AgentInput& operator=(const AgentInput&) = delete;

	// Start a script. Returns false if one is already running (caller answers "busy").
	bool BeginScript(const AgentScript& rScript);
	bool IsScriptActive() const { return mbScriptActive; }

	// Client drain point (main thread, before ImGui NewFrame): advance the active script one step and queue this
	// frame's ImGui IO events.
	// The main loop runs ImGui + scripts even while the window is minimized (GameBase::Render / ImGuiManager::Prepare
	// are unconditional; only swapchain recreation defers at a 0x0 extent), so no minimized fast-fail is needed —
	// verified 2026-07-09.
	void AdvanceFrame();

	// End of RawInputManager::Update: OR the synthetic key / mouse-button / mouse-pos state onto the just-published
	// snapshot so game edge-detection fires as with hardware. The scroll accumulator is NOT added here — it is a
	// lifetime accumulator folded into iScrollWheelValue on every publish (see SyntheticScrollAccumulator()).
	void Overlay(RawInput& rRawInput);

	// Persistent synthetic scroll offset added into the published lifetime iScrollWheelValue on EVERY publish (script
	// active or not) — consumers diff iScrollWheelValue, so the offset must never drop out of the published value.
	int SyntheticScrollAccumulator() const { return miSyntheticScrollAccumulator; }

	// Deferred-response poll accessors (valid once ScriptStatus() != kPending).
	AgentScriptStatus ScriptStatus() const { return meStatus; }
	bool ResolvedDisabled() const { return mbResolvedDisabled; }

private:

	void Finish(AgentScriptStatus eStatus);
	// Resolve the target label to this frame's rect and drive the stabilization loop. Returns true once stable;
	// on not-found / ambiguous / timeout it calls Finish() and returns false.
	bool StabilizeTarget();
	void IssueImGuiMousePos(float fX, float fY);

	bool mbScriptActive = false;
	AgentScript mScript {};
	AgentScriptStatus meStatus = AgentScriptStatus::kPending;

	int32_t miPhase = 0;
	int32_t miPhaseFrame = 0;
	int32_t miElapsedFrames = 0;
	int32_t miStableCount = 0;
	XMFLOAT4 mf4LastRect {};
	bool mbHaveLastRect = false;

	bool mbResolvedDisabled = false;
	int32_t miResolvedStatusFlags = 0; // resolved target's ImGuiItemStatusFlags (set_slider Inputable pre-validation)
	float mf2TargetCenter[2] {}; // resolved rect center, re-pinned into ImGui each frame

	// Persistent synthetic state read by Overlay() (game-binding sink).
	bool mpbSyntheticKeys[0xFF] {};
	uint32_t muiSyntheticMouseButtons = 0;
	bool mbSyntheticMousePosValid = false;
	float mf2SyntheticMousePixels[2] {};
	int miSyntheticScrollAccumulator = 0;
};

inline AgentInput* gpAgentInput = nullptr;

} // namespace engine

#endif // defined(BT_CLIENT)
