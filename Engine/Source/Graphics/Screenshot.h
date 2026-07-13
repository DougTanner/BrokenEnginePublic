#pragma once

#if defined(BT_CLIENT)

namespace engine
{

struct ScreenshotRequest;
struct DumpRenderTargetRequest;

// Capture the present swapchain image per rRequest (downscale + JPG/PNG), saved asynchronously. Called only from
// the RenderMainPresentAcquire capture site (fence-signal precondition documented there / in Screenshot.cpp).
void SaveScreenshot(int64_t iFramebufferIndex, const ScreenshotRequest& rRequest);

// Read back the named render target per rRequest and encode it (grayscale-normalized PNG for single-channel,
// direct PNG for 4x8-bit, optional raw .bin), saved asynchronously. Called only from the same capture site.
// Waits the per-framebuffer fence (like SaveScreenshot) so all in-frame compute/transfer/graphics work draining
// into the readback is complete regardless of the target's last-access scope.
void DumpRenderTarget(int64_t iFramebufferIndex, const DumpRenderTargetRequest& rRequest);

// Trust-boundary validation for a dump request (called from the agent handler during Drain): throws
// std::runtime_error on an unknown name (message lists valid names) or an out-of-range index/channel.
void ValidateDumpRenderTargetRequest(const DumpRenderTargetRequest& rRequest);

// Thread-safe capture-result slot. The async save/encode thread publishes the finished agent "result" JSON; the
// AgentCommandServer deferred-response poll (main thread) drains it via TakeCaptureResult. There is one active agent
// request/result slot, but abandoned screenshot and dump encoders may overlap because their independent futures
// outlive timeout/disconnect. ResetCaptureResult records a fresh active token; SetCaptureResult rejects stale-token
// publications, and TakeCaptureResult consumes only the active request's result.
uint64_t ResetCaptureResult();
void SetCaptureResult(uint64_t uiCaptureToken, nlohmann::json result);
std::optional<nlohmann::json> TakeCaptureResult(uint64_t uiCaptureToken);

} // namespace engine

#endif // defined(BT_CLIENT)
