# NHL24 PC performance roadmap

Integration branch: `performance/cinematic-renderer-overhaul`, from `d2d799b7`. Evidence and baseline numbers are in `PERFORMANCE_FINDINGS.md`.

## Checkpoint 1: profiling and low-risk fixes (implemented, awaiting gameplay test)

| Change | Files | Default | Toggle |
|---|---|---|---|
| Per-draw phase CPU timers and real render-pass begin counter | `diag_log.*`, `vk_rasterizer.cpp`, `vk_scheduler.cpp` | Timers off, counter on | `NHLPORT_DIAG_DRAW_PHASES=1` |
| Array-blank crest check caches layer hashes by guest content, so repeat refreshes skip the GPU download and `Finish()`; zero buffer allocated only when blanking | `texture_cache.*`, `core/memory.*` | On | none (blanking decisions unchanged) |
| Pipeline cache IO thread: atomic request counter, queue drained on shutdown | `cache_storage.cpp` | On | none |
| Driver pipeline cache saved on a worker 10 s after the last runtime compile, and at shutdown | `vk_pipeline_cache.*`, `vk_rasterizer.cpp` | On | `NHLPORT_DRIVER_CACHE_AUTOSAVE=0` |
| Session analysis: sustained clusters, workload correlation, phase breakdown, build comparison | `tools/perf_report.py` | n/a | n/a |

Verification so far:
- Clang 18 syntax-only compilation of every changed translation unit (Linux).
- Independent code review.
- Not yet built on Windows, run in game, or benchmarked.

## Next phases, in evidence order

1. **Render-pass churn (Phase 2).** Use `vk_rendering_begins` per draw from the checkpoint run.
   - If the ratio is high, track read and write stages separately in `AccessBuffer`.
   - Flush render-target barriers only when they are pending.
   - Merge the feedback-loop access masks.
   - Validate with the synchronization validation layers.
2. **Per-stage specialization cache (Phase 2).** Applies if the `pipeline` phase dominates.
   - Key: program address, binary hash, flattened user data, runtime info, start bindings.
   - Plus a last-key fast path.
   - Invalidate on new permutations, shader replacement, and code changes.
3. **Texture and target binding caches (Phase 2).** Applies if `resources`/`targets` dominate.
   - Key: raw T#/S# and color/depth register snapshots.
   - Validated by a texture-cache generation counter and image flags.
4. **Redundant command emission (Phase 2).**
   - Skip unchanged vertex input, vertex/index binds, push constants and push descriptors within a command buffer.
5. **Readbacks (Phase 5).**
   - Invalidate `ReadFile` targets after the read, with the real byte count.
   - Skip downloads for pages that are fully overwritten (avplayer, mprotect without write loss).
   - Flush early for windows with repeated readbacks.
6. **Expansion (Phase 3).**
   - Delta expansion: GPU-copy the old mips and upload only the new ones.
   - Skip the refresh when a render target resizes and its GPU data is copied anyway.
   - Then try full-capacity allocation once a growth chain is confirmed within guest mapping bounds.
7. **Image recycle pool (Phase 3).**
   - Bounded, keyed on the full create info, released only after GPU completion.
8. **Override caches (Phase 6).** Content caches for the portrait and main-pack overrides.
9. **Pipeline identity (Phase 7).**
   - Content-hash permutation identity.
   - Serialize the discard fragment shader and preload with the matching runtime info.
   - Normalize dynamic-state key fields.
   - Needs one cache version bump, which forces one full recompile.
10. **Parallel resource preparation and async pipelines (Phases 4, 7, 10).** Start only after 1–8 are measured.

Filesystem AIO (Phase 9) stays deprioritized: correlation with FPS is −0.05.

## Required gameplay test (checkpoint 1)

Build with `tools/build_nhlport.ps1`, stage with `tools/make_release.ps1`, then run with `NHLPORT_DIAG=1 NHLPORT_DIAG_DRAW_PHASES=1`:
- Be a Pro: arena load, anthem, opening faceoff.
- Several minutes of play with two or more goals and replays.
- An intermission.
- Press F1 (timing only) at the start of each cinematic.

Then run `python tools/perf_report.py release/user/log --compare <baseline log dir>`.

Visual checks:
- Utah jerseys still hide the Coyotes crest and shoulder patches.
- Other teams' crests are unchanged.
- Controller prompts and portraits are unchanged.
