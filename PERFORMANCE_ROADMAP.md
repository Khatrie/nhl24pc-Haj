# NHL24 PC performance roadmap

Integration branch: `performance/cinematic-renderer-overhaul`, from `d2d799b7`. Evidence is in `PERFORMANCE_FINDINGS.md`; the operational state and test protocol are in `AGENT_HANDOFF.md`.

## Done

| Change | Default / toggle | Result |
|---|---|---|
| Session analysis tool `tools/perf_report.py` | n/a | In use for every run |
| Stacked diagnostic logs across runs, `--trend` / `--session` / `--list-sessions` | On; `NHLPORT_DIAG_APPEND=0` disables, `NHLPORT_DIAG_STACK_MB` caps file size (default 1024) | Tested offline; first in-game run pending |
| Per-draw phase timers, binding sub-phases, per-buffer steps | `NHLPORT_DIAG_DRAW_PHASES=1` (launch with `tools\run_draw_phase_profile.cmd`) | Attributed about 11.4 µs/draw on the command thread |
| `vk_rendering_begins` counter | Always on | Render-pass churn ruled out (0.09 begins per draw) |
| Array-blank crest check content cache | On | Crest still hidden; no FPS change (it was not the bottleneck) |
| Pipeline cache IO thread drain and atomic counter | On | Correctness fix |
| Driver `.vkcache` autosave after runtime compiles | On; `NHLPORT_DRIVER_CACHE_AUTOSAVE=0` disables | Works, about 46 ms on a worker |
| `make_release.ps1` keeps user settings | `-ResetUserConfig` writes the template | Not yet exercised |
| Profiling counters isolated and gated (`c6e0fe98`) | n/a | Removes a regression of about 2 µs/draw; needs verification run 1 |

Rejected: reuse of identical stream-buffer copies. 70% of bytes were reusable, but there was no reduction in binding cost; reverted.

## Pending gameplay tests

0. **Stream copy reuse (`tools\run_stream_reuse.cmd`, `NHLPORT_STREAM_REUSE=1`):** compare CP µs/draw with run `20261127-154844` (11.54), and check visuals. Stream binding is 0.39 µs fixed plus 0.071 µs/KiB per buffer, at about 6 per draw.

1. **Run 1, profiling off:** confirms the command thread is back to about 11.4 µs per draw on the current build.
2. **Run 2, profiling on:** provides the per-buffer step breakdown, which selects item 1 below.

## Next work, in evidence order (slow-window µs per draw)

1. **Buffer binding (5.5 µs, about 12 buffers per draw).** Target the step that run 2 identifies:
   - **Stream path:** small read-only buffers are re-copied every draw. Reduce fixed per-buffer overhead in `BufferCache::ObtainBuffer`:
     - one region check per contiguous range,
     - a single stream `Map` per stage for all small buffers,
     - avoid repeated alignment and watch bookkeeping.
   - **Cached path:** skip `SynchronizeMemory` work for ranges with no CPU-modified pages. Check this cheaply first.
   - **GPU invalidate:** skip `InvalidateMemoryFromGPU` for ranges with no registered images, or ranges already invalidated in the same tick.
   - **Sharp reads:** cache V# reads per stage when the flattened user data is unchanged.
2. **Pipeline lookup (1.6 µs).**
   - Per-stage specialization cache keyed on program address, binary hash, flattened user data contents, runtime info and start bindings.
   - Plus a last-key fast path.
   - Invalidate on new permutations, shader replacement and code changes.
3. **Texture lookup and views (2.1 µs).**
   - T#/S#-keyed binding cache, validated by a texture-cache generation counter and image flags (`needs_rebind`, dirty bits).
4. **Descriptor push (1.2 µs).** Skip pushes whose layout and payload match the previous draw in the same command buffer.
5. **Cleanup and recording (1.5 µs).**
   - `ResetBindings` access tracking.
   - Avoid per-draw mutex and `DebugState` shared-lock checks in `liverpool.cpp`.
6. **Readbacks (26–36 ms/s of GPU wait).**
   - Post-read invalidation for `ReadFile`.
   - Discard downloads for fully overwritten pages.
   - Earlier flush for windows with repeated readbacks.
7. **Texture expansion and image creation (spikes of 100–480 ms).**
   - Delta expansion: GPU-copy the old mips and upload only the new ones.
   - Skip the refresh on render-target resize.
   - Bounded image recycle pool.
8. **Pipeline cache identity.** Content-hash permutation identity, plus serializing the discard fragment shader. Needs one cache version bump.
9. **Parallel draw preparation and async pipelines.** Only after 1–5 are measured.

Deprioritized: filesystem AIO (correlation with FPS is −0.05) and render-pass/barrier churn (ruled out).

## Gameplay test protocol

See `AGENT_HANDOFF.md` §4. Use the same settings every run (FIFO, 1080p windowed, RCAS on). Launch through the profiling script when phase data is needed. Runs stack in the same log files; track them with `perf_report.py --trend` and compare µs per draw rather than raw totals.

Visual checks after texture or binding changes:
- the Utah crest is hidden,
- other crests are unchanged,
- controller prompts and portraits are unchanged.
