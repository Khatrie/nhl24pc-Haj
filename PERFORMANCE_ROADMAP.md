# NHL24 PC performance roadmap

Integration branch: `performance/cinematic-renderer-overhaul`, from `d2d799b7`. Evidence is in `PERFORMANCE_FINDINGS.md`; operations and the test protocol are in `AGENT_HANDOFF.md`.

**Goal:** cinematics at a steady 55–60 FPS without changing rendering.

**Bottleneck:** the GPU command processor thread, at about 11.4 µs of CPU per draw. Cinematics send about 3,200 draws per frame, so 60 FPS needs about 4.6 µs per draw: roughly 60% less.

## Done

| Change | Default / toggle | Result |
|---|---|---|
| Session analysis `tools/perf_report.py` | n/a | Used for every run |
| Stacked diagnostic logs, `--trend` / `--session` / `--list-sessions` | On; `NHLPORT_DIAG_APPEND=0`, `NHLPORT_DIAG_STACK_MB` | Verified in game |
| Per-draw phase timers | `NHLPORT_DIAG_DRAW_PHASES=1` (`tools\run_draw_phase_profile.cmd`) | Works, but adds about 4 µs/draw |
| **Sampling profiler of the command thread** + linker map | `NHLPORT_CP_SAMPLE=1` (`tools\run_cp_profile.cmd`), `tools/cp_profile.py` | 1.7% suspension, about 0.8 µs/draw; see findings |
| `vk_rendering_begins` counter | On | Render-pass churn ruled out |
| Crest array-blank content cache | On | Correct; no FPS change |
| Pipeline cache IO drain, driver `.vkcache` autosave | On; `NHLPORT_DRIVER_CACHE_AUTOSAVE=0` | Works |
| `make_release.ps1` keeps user settings; `tools\build_and_stage.cmd` builds the exe and launcher and stages them | n/a | Used for every build |
| Ports from HajDEO/nhl24pc: red-zone crash fix (always on), save-data fix, pipeline cache rebuild, C++ exception filter, stale-alias VRAM release | On | Clean runs |
| A: stream-buffer reads not barrier-tracked (exact) | `NHLPORT_OPT_STREAM_UNTRACKED=1` | 11.65 µs/draw: no gain |
| B: ImageInfo cache keyed on the T# (exact) | `NHLPORT_OPT_TEX_INFO_MEMO=1` | With A: 11.24 µs/draw, about 2%, at the noise limit |

**Rejected:**
- **Stream-copy reuse within a submission** (`30fd8026`, reverted): 10% hit rate, 14.19 µs/draw, and unsound against direct guest CPU writes.
- **Hashed reuse of identical stream copies:** no gain.

## Pending decisions

1. **Make A+B default on.** Both are exact, and their best run was 2% faster. This can go into the next build.
2. **Start the multi-threaded command processing below.** Waiting for the owner's go-ahead.

## Proposed: multi-threaded command processing

### Why

The profile (session `20261127-172849`, slow seconds) shows that small exact fixes each save 1–3%. The command thread's time splits into four kinds of work:
- preparing draws in our own code: about 45–55%,
- inside the NVIDIA driver recording commands: about 25%, mostly push descriptors and draw recording,
- copying guest memory: about 10%,
- waiting for readbacks: about 7%.

Draws must stay in guest order and share mutable state (registers, texture cache, buffer cache, barriers), so the work cannot be split *across draws*. It can be split into **pipeline stages**, where each thread does one stage for every draw in order. Throughput is then limited by the slowest stage.

### Stages (each behind a switch, measured with the owner before the next)

**T0. Readback waits off the critical path (small).**
- Today `BufferCache::ReadMemory` runs its readback on the command thread through `SendCommand` and blocks in `Semaphore::Wait`: 5.8% of slow time.
- Move the wait and the copy-back to a helper thread.
- The command thread continues with packets that do not depend on the data.
- The faulting guest thread still waits for its data, which is the correct behaviour.
- This also proves the threading and synchronization scaffolding.
- **Expected:** up to about 5–7%.

**T1. Split *prepare* from *record* (the main gain).**
- **The command thread keeps:**
  - PM4 decoding,
  - register state,
  - pipeline lookup,
  - texture and buffer cache lookups and uploads,
  - the decision of what to bind.
- **The command thread emits:** a compact, ordered command stream with everything the driver call needs: pipeline handle, push-descriptor payload copied by value, dynamic state, vertex and index bindings, draw parameters, barriers.
- **A recorder thread** owns the Vulkan command buffer and replays the stream into it.
- **Correctness requirements:**
  - Every Vulkan command, including texture-cache copies, uploads, clears and barriers, goes through the stream in guest order.
  - Objects referenced by queued commands stay alive until recorded, through the scheduler's deferred destruction.
  - `Scheduler::Flush`, `Finish` and `Wait`, called from the command thread, first drain the stream.
  - Submission stays ordered.
  - The stream is a bounded ring, with back-pressure and no busy-waiting.
- **Expected:** throughput bounded by the prepare stage, about 55–60% of today: roughly 6.5–7 µs per draw.

**T2. Memory-copy helper.**
- Large guest-to-staging copies (texture refresh and expansion, large buffer uploads) run on a worker.
- Small stream copies stay inline: worker scheduling would cost more than the copy.
- Mapping lifetime and coherency rules are as in the project instructions (Phase 5).
- **Expected:** a few percent on top, plus fewer hitches during texture streaming.

**Not planned:** more than 3 threads. The prepare stage would stay the bottleneck, and splitting it needs the texture and buffer caches redesigned for concurrent access.

Parallel recording of the stream into several secondary command buffers is possible later, if the profile shows recording becoming the bottleneck.

**Expected result of T0–T2:** about 6.5 µs per draw. Most cinematics would land in the mid-50s to 60 FPS; the heaviest scenes would still dip.

**Risks:**
- Ordering and lifetime bugs: corruption, flicker or crashes.
- Deadlocks around `Finish` and `Wait`.
- Readback ordering against later packets.

Every stage keeps its switch until it has run cleanly across several sessions.

## Smaller exact fixes (backlog, from the profile)

| Item | Slow-second share | Idea | Risk |
|---|---:|---|---|
| `SynchronizeMemory` page re-protection | 7.1% incl. (`VirtualProtect` 3%) | Batch protection changes per draw or per region | Must not miss CPU writes between upload and protect |
| Pipeline lookup | 7.1% incl. (`StageSpecialization` ctor+dtor 4.5%) | Reuse one specialization object instead of building and destroying one per stage per draw; per-stage last-key fast path | Low if the comparison stays exact |
| Push descriptors in the driver | about 5.4% | Skip identical pushes; longer term, descriptor buffers | Low (skip) to high (redesign) |
| Texture lookup and views | `FindImage` 6.5%, `FindView` 1.8% | T#-keyed binding cache validated by a texture-cache generation counter | Medium (invalidation) |
| Stream `memcpy` | 6.5% | Little to gain: it is near memory bandwidth | — |

## Other open items

- **Shared-memory barrier fix for split GCN waves** (from HajDEO `6b08321a`): a correctness fix for NVIDIA compute shaders. It needs a shader cache version bump and a one-time recompile, so give it its own checkpoint.
- **Pipeline cache identity:** permutation order, and preload of the discard fragment shader. Share the same version bump.
- **Texture expansion and image-creation spikes** (100–480 ms): delta expansion, skipping the refresh on render-target resize, a bounded image recycle pool.
- **Push the branch** to `origin`.

**Deprioritized:**
- filesystem AIO (correlation with FPS is −0.05),
- render-pass and barrier churn (ruled out),
- copy-reuse schemes (rejected twice).

## Test protocol

See `AGENT_HANDOFF.md` §4. Compare CP µs/draw across runs. Noise is about 0.15–0.3 µs, and FPS and slow seconds depend on scene mix.

Visual checks after texture or binding changes:
- the Utah crest is hidden,
- other crests are unchanged,
- controller prompts and portraits are unchanged,
- no flicker or missing textures.
