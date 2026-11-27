# NHL24 PC performance findings

Branch `performance/cinematic-renderer-overhaul`, based on `d2d799b7`. Analysis tool: `tools/perf_report.py`.

## Baseline

Source: the 82-minute Be a Pro session from November 26, 2026 (`release/user/log`, build `d6fdc8bf-dirty`). That build differs from `d2d799b7` only in `texture_cache.cpp`, because of the controller prompt-pack commit. Its environment had `NHLPORT_DIAG_FRAMES=1`, FIFO at 60 Hz, a 1920×1080 internal resolution, Relaxed readbacks, and pipeline caching enabled.

| Metric | Value |
|---|---:|
| Frames / duration | 285,238 / 81.9 min |
| Average FPS | 58.0 |
| P50 / P95 / P99 / P99.9 frame time | 16.67 / 17.10 / 33.41 / 66.46 ms |
| 1% / 0.1% low | 22.4 / 8.3 FPS |
| Frames > 50 ms / > 100 ms | 499 / 46 |
| Sustained < 55 FPS (≥ 3 s clusters) | 69 clusters, 378 s |
| Sustained < 45 FPS | 33 clusters, 156 s |
| Sustained < 30 FPS | 1 cluster, 3 s |
| Total pipeline + shader compile | 1.75 s |
| Total buffer readback wait (GPU command thread) | 161 s |

Only one one-second window looked like an intentional guest 30 FPS cadence. The slow intervals are genuine slowdowns.

## Confirmed by measurement

1. **Draw-call throughput on the GPU command thread limits cinematic FPS.**

   | Draws/s | Windows | Mean FPS | GPU command processor CPU |
   |---|---:|---:|---:|
   | 0–40k | 730 | 59.5 | 31% |
   | 40–60k | 2,373 | 59.6 | 66% |
   | 60–80k | 1,278 | 58.5 | 82% |
   | 80–100k | 392 | 51.2 | 89% |
   | 100–120k | 123 | 42.4 | 92% |

   - Draws correlate with window FPS at r = −0.49, the strongest workload correlation.
   - GPU busy time stays at 40–50%.
   - At about 90% of one core and about 90k draws/s, the whole thread costs about 10 µs per draw, PM4 processing included.
   - Slow windows also average 5.4× the texture refresh volume (237 vs 44 MiB/s), 4.2× image creation, and 9.8× expansions.

2. **Buffer readbacks block the command thread for 161 s per 82 minutes.**
   - That is 0.56 ms per frame on average, with a p99 of 5.5 ms and a maximum of 56 ms.
   - Readbacks are the leading timed component in 105 of the 499 frames over 50 ms.

3. **Frames over 50 ms, by heuristic cause:**

   | Cause | Frames |
   |---|---:|
   | Texture streaming | 262 |
   | Readback | 105 |
   | Unattributed | 100 |
   | Compilation | 15 |
   | Not rendering | 14 |
   | File reads | 3 |

   Compilation dominates the very largest spikes (374 ms, 178 ms), but they are rare.

4. **The 10.57 s pause at t = 2191.6 s is not a rendering stall.**
   - That frame has zero draws.
   - The key guest simulation thread recorded about 10,047 ms of CPU time in the frame.
   - The guest was busy, or spinning, on its simulation thread.
   - Capture wait and CPU stacks if it recurs.

## Diagnostic caveats found

- `audio_wait_ms` tracks frame time (r = 0.996). It is a symptom, not a cause.
- Per-frame thread columns (`gpu_thr_ms`, `cpu_ms`, `sim_ms`, …) are quantized to 15.625 ms. Use `nhl24_threads.csv` or windowed sums.
- The `render_passes` column counts `Rasterizer::BeginRendering` calls, which means one per draw. It does not count real Vulkan render passes. The new `vk_rendering_begins` column counts real begins.

## Code findings (source review of d2d799b7; costs are estimates until profiled)

### Command processor (per draw)

- **No register dirty tracking.** Every draw rebuilds:
  - the pipeline key,
  - per-stage specialization (`RefreshGraphicsStages` → `GetProgram`), estimated 1.5–4 µs per draw,
  - every texture lookup (`BindTextures`: `ImageInfo` construction, `FindImage`, `FindTexture`, sampler hash), about 0.5–1 µs per texture,
  - render-target and depth lookups and `LinkMeta`.
- **Render-pass churn (likely).**
  - `AccessBuffer` adds read-only stages to the pending barrier mask, so the early return in `FlushBarriers` effectively never fires.
  - Every flush ends dynamic rendering.
  - Feedback-loop targets alternate ShaderRead and attachment access, which adds a barrier on every such draw.
  - Verify with `vk_rendering_begins` before changing anything.
- **Vertex input, vertex/index buffer binds, push constants and push descriptors** are re-emitted on every draw.
- **Small fixed costs:**
  - `DebugState.DumpingCurrentReg()` takes a shared_mutex per draw.
  - `PopPendingOperations` takes a mutex per draw.

### Texture cache

- **Fixed in this branch: a default-on full GPU drain.**
  - `ApplyTextureOverride`'s array-blank path defaults to `512x512:Bc3Srgb:0f:H`. It hides the leftover Coyotes crest by content hash.
  - With no pack or environment variable, it downloaded mip 2 of up to 4 layers and called `scheduler.Finish()` on **every** refresh and expansion of a 512×512 BC3-sRGB texture array.
  - It now caches layer hashes by guest content plus layout. See the roadmap.
- **Expansion re-uploads every mip from guest memory, then copies old mips over them.**
  - Mip memoization does not help the first 1→N growth chain.
  - Render-target size changes at one address expand in both directions.
- **No image or memory reuse.** Freed images are destroyed by `vmaDestroyImage`.
- **`ApplyPortraitOverride` and the main override** download and `Finish()` on every matching refresh, with no positive or negative cache. They only run when their files are present.

### Readbacks (Relaxed mode)

- A guest CPU write to a GPU-modified page blocks the faulting thread until the GPU thread reads the data back.
- If the write is still in the open command buffer, that submission is flushed and waited on.
- Emulator-internal writers take the same path: `ReadFile` into GPU-modified pages, avplayer output, and `sceKernelMprotect`. These could skip downloads for pages that will be overwritten entirely.

### Pipelines

- `WarmUp()` builds every saved key synchronously at startup. Runtime compiles mean the key was never saved, was skipped at preload, or no longer matches.
- **Permutation identity depends on visit order.** It is `perm_idx` (`modules.size()`), so keys can churn between sessions and overwrite `.spv`/`.meta` files. Not changed yet, because it needs a cache version bump.
- **Preload drops the clip-distance discard fragment shader.** `SerializationSupport` never serializes `fragment`, and preload passes the stale `runtime_infos`. NVIDIA needs clip-distance emulation. Not changed yet, because it needs a cache version bump.
- **The driver `.vkcache` was saved only after startup warm-up.** Fixed in this branch.
- **The pipeline cache IO thread dropped queued writes at shutdown, and its request counter was a data race.** A lost decrement left the thread spinning on an empty queue. Fixed in this branch.

## Rejected or deprioritized hypotheses

- **Disk streaming as the main cause.** 327 of 527 frames over 50 ms read no files, and correlation with FPS is −0.05.
- **Shader compilation as the cause of recurring cinematic slowdowns.** Slow windows average 2.5 ms of compilation per second, and 455 of 477 frames over 50 ms compiled nothing.

## Remaining uncertainty

- The per-phase split of the about 10 µs per draw is unmeasured. That is the purpose of the next profiling run.
- None of this branch's changes has been measured in gameplay yet.
