# Screenshot stb Header Centralization

## Context
Follow-up from the `Architecture_UnusedIncludes` execution. That plan intended to remove `Screenshot.cpp`'s relative-path `#include "../../../ThirdParty/stb/stb_image_write.h"` on the premise it was already provided by `Common/ExternalHeaders.h`. The premise was **false** (caught at self-audit): `ExternalHeaders.h`'s `stb_image_write.h` include lives inside the `#if defined(BT_DATA_PACKER)` block (`ExternalHeaders.h:276`+), so it is **not** client-visible. `Engine/Source/Graphics/Screenshot.cpp:5` is therefore the sole declaration source of `stbi_write_jpg` (used at `Screenshot.cpp:70`) for the client build, and the local include is required — so it was kept.

This leaves a real (small) inconsistency: `renderdoc_app.h`, `DirectXTK`, and `PerlinNoise` are all client-only third-party consumption headers routed through a `#if defined(BT_CLIENT)` block in `ExternalHeaders.h` (renderdoc just moved there in the parent plan), but the client's stb-write consumption is a scattered relative include instead. `ExternalHeaders.h`'s own design comment says third-party consumption includes should "live in one place rather than scattered."

## Design
Mirror the renderdoc move: add `#include "stb/stb_image_write.h"` to a `#if defined(BT_CLIENT)` span in `Common/ExternalHeaders.h` (declarations only — the `STB_IMAGE_WRITE_IMPLEMENTATION` unit stays in `ThirdParty/Prebuilts/Source/Engine/Stb.cpp`, which the client already links, since `Screenshot.cpp` links `stbi_write_jpg` today). Then delete the relative `#include "../../../ThirdParty/stb/stb_image_write.h"` from `Screenshot.cpp` — it becomes PCH-provided, obeying the documented Pch-provided-header rule.

Verify: the `stb` include directory resolves from `ExternalHeaders.h`'s compile context for the client (the client vcxproj already has `ThirdParty` on `AdditionalIncludeDirectories`; the DataPacker block already uses the bare `"stb/stb_image_write.h"` form). Confirm no double-definition (only the impl-macro TU defines the implementation; adding the declaration header to the client PCH is safe). Compile client + server.

Trade-off to weigh at grill: routing it through the PCH means every client TU parses the stb-write header for a single consumer (`Screenshot.cpp`). Alternative minimal option: keep the include local but normalize the ugly relative path to the include-dir-relative `"stb/stb_image_write.h"` form. Recommend the PCH route for convention-consistency unless the added PCH parse cost is a concern.

## Critical files
- `Common/ExternalHeaders.h` (new `BT_CLIENT` stb-write consumption include)
- `Engine/Source/Graphics/Screenshot.cpp` (drop the local relative include)

## Out of scope
- Other stb headers (`stb_image.h`, `stb_image_resize2.h`) — currently DataPacker-only; only add a client span for one if a client consumer actually needs it.
- The broader Pch-provided-header re-include sweep (separate follow-up).

## Notes
- Invariant exposure: none — include-only, compile-checked; both builds must compile (server is unaffected — `Screenshot.cpp` is client-only).
- Grill decision: PCH-route (recommended) vs keep-local-but-normalize-path.
