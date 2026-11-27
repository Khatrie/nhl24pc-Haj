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

## Measured GPU command thread cost per draw (November 27 runs, `NHLPORT_DIAG_DRAW_PHASES=1`)

Run `phases2 11-27-26` (build `3113db94`, FIFO, 1080p windowed, RCAS on), slow windows. Totals include timer overhead and the counter regression described below:

| Phase | µs/draw |
|---|---:|
| Resource binding | 7.7 |
| — buffers (about 12 per draw, 0.42 µs each) | 5.5 |
| — texture lookup (about 6.4 per draw, 0.19 µs each) | 1.3 |
| — texture views and layout transitions | 0.8 |
| — samplers | 0.1 |
| Pipeline lookup | 1.6 |
| Descriptor push | 1.2 |
| Command recording | 0.9 |
| Per-draw cleanup (`ResetBindings`) | 0.6 |
| Vertex/index binding | 0.6 |
| Everything else | 0.9 |
| **All draw work** | **14.8** |

- **Render passes are not churning.** Real dynamic-rendering begins run at about 0.09 per draw.
- **Small read-only buffers are copied into the stream buffer on every draw.** That is about 15 KiB per draw, roughly 1 GiB/s in cinematics.
- **Bytes are not the cost; per-buffer handling is.**

### Regression from always-on profiling counters (fixed)

The sub-phase commit (`7944dcfe`) updated shared atomic counters about 15 times per draw even with profiling off. Median command-thread cost per draw in windows with at least 40k draws/s:

| Build | µs/draw |
|---|---:|
| Baseline / checkpoint 1 | 11.5 / 11.4 |
| Checkpoint 1 with profiling | 12.2 |
| `7944dcfe`, profiling off | 13.6 |
| `3113db94`, profiling on | 15.0 |

The likely cause is cache-line sharing with counters written by other threads. The counters now live in a separate cache-aligned block and are updated only while profiling. This fix still needs a gameplay check.

## Buffer binding steps (run `20261127-155502`, build `fca8b5eb`, profiling on)

| Step | µs/draw | µs per buffer |
|---|---:|---:|
| `obtain_stream` | 4.06 | 0.62 |
| `obtain_cached` | 1.68 | 0.36 |
| sharp | 0.31 | 0.03 |
| GPU invalidate | 0.35 | 0.03 |
| access check | 0.31 | 0.03 |

- **Per draw:** 6.0 stream-copied and 4.2 cached buffers per draw/dispatch, about 1 GiB/s copied into the stream buffer.
- **Cost model:** a least-squares fit over 164 one-second windows gives `obtain_stream` = 0.393 µs per buffer + 0.071 µs per KiB (R² 0.981; average buffer 3.2 KiB).
  - The fixed part is about 63% of the stream-binding cost.
  - The copy itself runs near memory speed (about 14 GiB/s).
  - The fixed cost matches several cache misses per buffer: tracker bits, page table, the first source line and the ring.
- **Profiling overhead:** 15.87 vs 11.54 µs/draw with profiling off (run `20261127-154844`). Mid-ice drops to about 47 FPS with the timers on.

## Rejected or deprioritized hypotheses

- **Reusing stream-buffer copies inside a submission, without hashing (`NHLPORT_STREAM_REUSE`, `30fd8026`, reverted).**
  - Run `20261127-161744`: only 8–11% of small read-only binds were reused.
  - Most repeats are separated by a packet that ends the window (any non-register, non-draw packet, or a CP coroutine switch). The earlier 70% figure counted repeats anywhere in a tick.
  - CP cost was 14.19 µs/draw against 11.54 (run `20261127-154844`). Average FPS fell from 57.6 to 53.0.
  - The likely overhead is the always-on epoch increment on every packet and guest-memory write, plus the 48 KB lookup table. A single run cannot rule out noise, but the ceiling at a 10% hit rate is about 0.3 µs/draw.
  - **Unsound as designed.** Guest CPU threads write memory directly, with no hook, so the epoch cannot prove that memory was unchanged between two draws.
  - Do not retry reuse keyed on command-stream position. It needs write tracking on guest pages, and per-bind lookups then cost as much as the copy.

- **Reusing identical stream-buffer copies within a submission (`NHLPORT_STREAM_DEDUP`, removed).**
  - 70% of the bytes were reusable.
  - Binding cost was unchanged: 7.3 µs per draw with reuse, 7.7 µs without.
  - Hashing each buffer costs as much as the copy it saves.

- **Disk streaming as the main cause.** 327 of 527 frames over 50 ms read no files, and correlation with FPS is −0.05.
- **Shader compilation as the cause of recurring cinematic slowdowns.** Slow windows average 2.5 ms of compilation per second, and 455 of 477 frames over 50 ms compiled nothing.

## Checkpoint 1 gameplay result (run `checkpoint1 11-27-26`)

Compared with the first five minutes of the baseline, which cover similar content:

| Metric | Baseline | Checkpoint 1 |
|---|---:|---:|
| FPS | 56.7 | 55.2 |
| Draws/s | 41k | 46k |
| GPU stall ms/s | 28.6 | 38.0 |
| Readback wait ms/s | 26.8 | 35.8 |
| Frames > 50 ms per minute | 11.4 | 9.7 |

- The result is neutral within scene variance.
- GPU stalls are almost entirely buffer readbacks, not the array-blank path.
- The owner confirmed the Utah crest, controller prompts and texture replacements are unaffected.
- The driver cache autosave logged 45.9 MB in 46 ms on its worker.

## Other observations

- **One crash to desktop.** Build `091a7bfb` crashed straight to desktop at 106 s, just after a scene switch into a 2112×2112 render target.
  - There was no crash dump, no log error, and no memory pressure (working set 7.5 GB, VRAM 3.8/7.2 GB).
  - The driver-cache worker was not running.
  - It has not recurred in four later runs.
- **Slow startup warm-up once.** Warm-up took 34.7 s once, against the usual 4.7 s, with the same cache. This is probably the NVIDIA driver cache.
- **Settings drift (cause fixed).** Run settings drifted (FIFO to Mailbox) because `make_release.ps1` rewrote `config.json`. The script now keeps existing user files.
- **Comparing runs.** The robust cross-run metric is the median command-thread µs per draw over one-second windows with at least 40k draws. Raw session totals are misleading.

## Remaining uncertainty

- Which per-buffer step owns the 5.5 µs of buffer binding. Run 2 (profiling on, build with `84f38682`) answers this.
- Whether `c6e0fe98` fully removes the counter regression. Run 1 (profiling off) answers this.
- The cause of the single crash to desktop.
