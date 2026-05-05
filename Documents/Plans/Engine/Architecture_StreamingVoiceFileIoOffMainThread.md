# Architecture: Move StreamingVoice File I/O Off the Main Thread

## Context

Music streaming buffer refill currently performs synchronous file I/O on the main thread. The `FillBuffer` member of `StreamingVoice` (`Engine/Source/Audio/StreamingVoice.cpp:74-112`) calls `gpFileManager->ReadChunkData` directly. Two main-thread call sites:

1. `StreamingVoice::StreamingVoice` constructor (`StreamingVoice.cpp:19-53`) — pre-queues all 3 buffers (3 x 16 KB reads) at track start. Reached from `StreamingVoices::CreateStream` (`StreamingVoices.cpp:201-214`), invoked by `Play` and `CheckTrackTransition`.
2. `StreamingVoices::SubmitBuffers` (`StreamingVoices.cpp:154-199`) — refills one buffer per consumed buffer per `Update`, called from `AudioManager::Update` post-render on the main thread.

A prior bug fix in this session pre-queued the 3 initial buffers in the constructor to eliminate music static/stuttering caused by single-buffer queue depth not surviving main-thread refill latency. That fix made the audible symptom go away on a warm SSD but did not address the underlying property: file I/O still runs on the main thread.

Per `Engine/Source/Audio/CLAUDE.md`, the existing design intentionally keeps file I/O off the XAudio2 callback thread (the callback only bumps `miBuffersConsumed`). The same reasoning applies to the main thread: the main loop is allocation-tracked (`DEBUG_BREAK` on heap allocs) precisely because main-thread latency matters. On a cold HDD or contended disk, a 16 KB synchronous read can spike to 10+ ms — a visible frame hitch every ~93 ms during music playback, plus a 30+ ms hitch at every track transition (3 reads back-to-back inside `StreamingVoices::Play` / `CheckTrackTransition`).

The hazard is real but currently invisible on the developer's warm SSD; it will show up on player machines with HDDs, fragmented drives, AV scanning, or background sync.

## Design

Move streaming buffer refill onto a worker thread using `common::PersistentWorker` (per `Common/CLAUDE.md`). Main-thread `SubmitBuffers` becomes a cheap operation that only calls `IXAudio2SourceVoice::SubmitSourceBuffer` against pre-filled buffers; the worker reads ahead of the consumer.

Sketch:

- Each `StreamingVoice` (or `StreamingVoices` as a whole — see open question below) owns a `common::PersistentWorker` that runs a fill loop.
- A small ring of pre-filled buffers (the existing `mBuffers[kiBufferCount]` may suffice) is shared between the worker (producer) and main thread (consumer / submitter).
- New per-buffer state: a slot status (`kEmpty` / `kFilling` / `kReady` / `kSubmitted`) so the worker knows which slots to fill and the main thread knows which slots are ready to submit. Likely an array of `std::atomic<uint8_t>` states with `memory_order_acquire`/`release` between worker and main thread.
- The XAudio2 callback (`OnBufferEnd`) continues to bump `miBuffersConsumed`; main-thread `SubmitBuffers` reacts by transitioning consumed slots from `kSubmitted` to `kEmpty`, which the worker then notices and refills.
- Worker wake-up: signal the worker on `OnBufferEnd` (already runs on the XAudio2 thread, which is already off-main) or on the main-thread transition to `kEmpty`. `PersistentWorker` already exposes the wake primitive — pick whichever matches its idiom.
- Constructor pre-queue: replace the synchronous 3-buffer fill with kicking the worker once and letting it fill the ring before `Start()` is called, OR make `Start()` deferred until the worker reports first buffer ready. The constructor cannot block the main thread for 3 reads either — same hazard, just at track-start.

## Critical files

- `Engine/Source/Audio/StreamingVoice.h` — add per-slot state array; consider whether `PersistentWorker` lives here or one level up.
- `Engine/Source/Audio/StreamingVoice.cpp` — split `FillBuffer` so the I/O portion is callable from the worker; constructor no longer performs reads inline.
- `Engine/Source/Audio/StreamingVoices.h` — possibly host one shared `PersistentWorker` here instead of one per voice (open question).
- `Engine/Source/Audio/StreamingVoices.cpp` — `SubmitBuffers` becomes consumer-only (no `FillBuffer` call); `CreateStream` / `TransitionCurrentToPrevious` / `Clear` must coordinate worker lifetime; deferred-destruction list (`mStreamsToDestroy`) must drain the worker before `~StreamingVoice`.
- `Engine/Source/Audio/CLAUDE.md` — update the "Buffer refill on main thread" bullet to reflect the new worker model once landed.

## Out of scope

- Replacing `gpFileManager->ReadChunkData` with async I/O at the file-manager layer. This plan moves file I/O off the main thread by running it on a worker; it does not change the file-manager API.
- Increasing `kiBufferCount` beyond 3 or changing `kiBufferSize` (16 KB). The hazard is the *thread* the read runs on, not the buffer geometry.
- Static voice loading (`StaticVoices`). Static voices load chunks at startup or on demand outside the per-tick audio loop; out of scope here.
- Crossfade / fade-in / fade-out logic in `StreamingVoice::UpdateVolume` — unchanged.
- Track-transition detection (`CheckTrackTransition`) — unchanged at the policy level. Only the `CreateStream` → `StreamingVoice` constructor path changes shape.

## Open design questions

1. **One worker per voice, or one shared worker for `StreamingVoices`?** Per-voice is simpler (lifetime trivially ties to the voice; no cross-voice scheduling). Shared is cheaper (current + previous + crossfade can total 3+ voices, each holding a thread is wasteful). Recommend: one shared worker on `StreamingVoices` that walks `mpCurrentStream` and `mPreviousStreams` each wake-up. Confirm with user.
2. **Queue depth on the worker side.** With 3 ring slots and the worker only acting when a slot transitions to `kEmpty`, no separate queue is needed — the slot-state array is the queue. Confirm this matches `PersistentWorker`'s idiom; if it expects a work-queue, model the queue as "voices with at least one empty slot."
3. **Error handling.** `gpFileManager->ReadChunkData` returning `false` currently sets `kLastBufferSubmitted` and stops the stream. Worker must propagate the same outcome via the slot-state machine without crossing thread boundaries with exceptions. Likely a per-voice `kFailed` flag the main thread reads on submit.
4. **Shutdown / drain sequence.** `StreamingVoices::Clear`, `~StreamingVoice`, and the `mStreamsToDestroy` deferred list must all stop the worker before destroying the voice. Specifically: (a) signal worker to exit, (b) join, (c) destroy XAudio2 voice. If a shared worker is used, it must skip voices being torn down rather than join-on-each-voice. Audio device-reset callbacks (`AudioManager`) clear streaming state inline — that path also needs a worker drain.
5. **Constructor blocking on first fill.** Either (a) constructor returns with no buffers submitted and `StreamingVoices::Update` later sees `kReady` slots and submits + calls `Start()`, OR (b) constructor synchronously waits on the worker for the first slot only. Option (a) is cleaner; option (b) preserves current "voice is playing immediately on return from `Play()`" behavior. Confirm with user — affects perceived responsiveness of `Play()`.
6. **Interaction with audio device reset.** Mid-fill device reset must not crash the worker; the worker should treat a null `mpVoice` as a stop signal. Verify against the `Suspend` / device-reset paths in `AudioManager`.

## Notes

- The fix for the original static/stuttering bug (pre-queueing 3 buffers in the constructor) remains correct as an interim solution and need not be reverted before this plan lands; the worker version supersedes it cleanly because the constructor will simply ask the worker to pre-fill instead of doing it inline.
- The XAudio2 callback path (`OnBufferEnd`) is already off-main. This plan only changes who refills, not who notifies.
- Once landed, the `// file I/O on main thread instead of XAudio2 callback` comment at `StreamingVoices.cpp:79` should be updated — file I/O will live on neither thread.
