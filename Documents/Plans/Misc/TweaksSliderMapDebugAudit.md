# Debug-Audit: Detect Slider Map Drift at First Frame

## Context

`engine::TweaksScreenBase::WrapperSlider()` in `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp` (line 83-138) silently no-ops when the `mapKey` lookup misses:

```cpp
auto it = rSliderMap.find(mapKey);
if (it == rSliderMap.end())
{
    return;
}
```

A typo on either the call site (`WrapperSlider("Player Depsoit Width", ...)`) or the registrar (`{"Player Depsoit Width", &gWindDepositPlayerWidth}`) silently dis-functions a slider with no error feedback. The user discovers it by clicking the slider and observing nothing changes — the slider draws at zero alpha because `bIsActiveSlider` is computed against an empty mapKey set.

## Goal

At debug-init time (gated by `if constexpr (kbDebugInput)`), audit the slider map: warn about every key that is registered but never referenced by a `WrapperSlider` call (orphan keys = dead Wrapper globals or stale labels), and every `WrapperSlider` mapKey that has no registration (typo or missing registrar entry).

## Approach

Cheaper than build-time tooling: run one "audit pass" on the first frame the tweaks UI is rendered. Two collection paths:

1. **Touched-key collection** — `WrapperSlider()` records `mapKey` into a side-set when an audit-mode flag is set. Render every section once with `mActiveSlider = "##audit-sentinel"` (a sentinel that no real slider matches), so `bIsActiveSlider` is false everywhere — but the map lookup and the touched-set insertion still happen. ImGui draws are issued at alpha 0 (matching existing exclusive-render-while-dragging behavior), so the audit costs one extra frame of zero-alpha draws but no visible UI noise.

2. **Diff** — after the audit pass, compare the touched set vs `TweaksSliderMap::Get()`'s key set:
   - In map but not touched: `LOG(kInfo, kWarning, "TweaksSliderMap: orphan key '{}'", key)`.
   - In touched but not in map: cannot reach this branch by construction (the lookup has already failed silently — the `WrapperSlider` early-return must be modified to insert a "miss" entry into a parallel `missed` set when audit mode is active, so we can warn on it).

Concrete shape:

```cpp
// In TweaksScreenBase
struct DebugSliderAudit
{
    std::unordered_set<std::string_view> touched;  // populated by WrapperSlider on hit
    std::unordered_set<std::string_view> missed;   // populated by WrapperSlider on miss
    bool bRunPending = true;
};

// On first Render() call when kbDebugInput, after mActiveSlider clear:
//   - set audit flag
//   - mActiveSlider = "##audit-sentinel"
//   - call all RenderSectionWindow() once each
//   - clear flag
//   - diff touched vs map keys, log warnings for orphans
//   - log warnings for misses
//   - set bRunPending = false
```

## Audit set storage

The slider map's bucket allocation is wrapped in `ScopedSuppressAllocationTracking`. The two audit sets must use the same discipline. Two options:

- **Workbuffer-backed sets**: build a `common::ScopedWorkbufferArena builder = common::gpThreadLocal->mWorkbuffer.Push();` accumulating "touched: ..." and "missed: ..." lines as they happen, then emit one `LOG` at audit-end. No `std::unordered_set` needed. Cheapest, but cannot dedupe so sliders rendered twice (e.g., subtab switches) show duplicate "touched" entries — acceptable since we only consume it for the diff against the registered map.
- **`std::unordered_set` wrapped in `ScopedSuppressAllocationTracking`**: matches the existing slider-map pattern. Cleaner diff logic, costs hash-bucket heap allocs gated by the suppression. Preferred — diff is `std::set_difference`-style and warning emission is per-entry, which the workbuffer approach can't easily do without sorting.

Pick option 2 for correctness; the audit runs once per session.

## Constraints

- **Compile-time elision**: entire path must be inside `if constexpr (kbDebugInput)`. Shipping builds compile away to nothing.
- **One-shot**: `bRunPending` self-disables after the first run. Recall on session reset is not needed (the slider map is program-lifetime).
- **No allocation-tracking trips**: every `std::unordered_set` insert/find must be in a `ScopedSuppressAllocationTracking` scope. `LOG` itself must use the workbuffer formatters (`common::Wb`, or a `ScopedWorkbufferArena` from `Workbuffer::Push()`) per project pattern; passing `std::string_view` directly to `LOG(..., "{}", key)` is allocation-safe.
- **Render side-effects**: the audit pass renders every section once with `mActiveSlider` sentinel. Every `WrapperSlider` call emits an ImGui slider, which should be inside an active `ImGui::Begin()` window. Either (a) wrap the entire audit in `Begin/End` of a hidden window (alpha 0, off-screen), or (b) skip the actual ImGui calls when audit-mode is active and only do the map lookup. (b) is simpler — gate the `ImGui::SliderFloat` and friends behind `!bAuditMode` inside `WrapperSlider`. Audit-mode does map lookup + touched/missed insertion only.
- **Section-render coupling**: `RenderSectionWindow` calls `(this->*kRenderSectionFunctions[iSection])()` — those functions issue not just `WrapperSlider` but also `WrapperSeparatorText`, `RenderWaveCountRadioButtons`, etc. Audit-mode must also early-return from those (no map involvement, but ImGui calls outside a `Begin` will assert in debug). Simpler alternative: wrap each section render call in a synthetic `ImGui::Begin("##audit", ...)/End` pair.
- **Sentinel choice**: `mActiveSlider = "##audit-sentinel"` ensures `bIsActiveSlider` is false for every real key, so existing alpha-0 draw paths trigger. But that path still calls `ImGui::PushStyleVar` / `ImGui::SliderFloat` — see prior bullet. Decide between (a) gate ImGui calls inside `WrapperSlider` on audit mode, or (b) wrap section calls in synthetic `Begin/End`. (a) is less invasive.

## Dependency on Plan 1

This audit is most valuable AFTER `TweaksScreenSliderMapPerTabRegistrars.md` (Plan 1) lands — the per-tab registrar refactor changes which file owns each registration string, and the audit's warning messages should already point at the new location. Landing audit before the registrar refactor would require a re-tuning of warning text. Run both in one session, registrar first.

## Files Touched

- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h` — add `DebugSliderAudit` member, audit-pass entry-point declaration
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp` — modify `WrapperSlider` to record touched/missed when audit-mode active, add `RunSliderAudit()` method called once from `Render()`

## Validation

- Build with `kbDebugInput == true`. Open the tweaks UI. First frame must emit zero warnings if all labels match.
- Introduce a deliberate typo in one `TweaksScreenWindDeposits.cpp` `WrapperSlider` call. Rebuild, run. Log must show one `kWarning` "TweaksSliderMap: missed key 'Player Depsoit Width'".
- Restore typo, deliberately rename a registrar key. Log must show one `kWarning` "TweaksSliderMap: orphan key '<old name>'".
- Build with `kbDebugInput == false`. Disassembly check: `RunSliderAudit` and the audit branch in `WrapperSlider` must be elided.
