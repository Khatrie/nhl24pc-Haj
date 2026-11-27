# NHL24 PC project status

Updated November 27, 2026. Active branch: `performance/cinematic-renderer-overhaul`, created from `codex/renderer-upstream-fixes` at `d2d799b7`. The branch is local and not pushed.

The cinematic slowdown is attributed to GPU command-thread cost per draw, about 11.4 µs. Buffer binding is the largest part, and the next work targets it. The operational state, verification runs and test protocol are in `AGENT_HANDOFF.md`. The plan is in `PERFORMANCE_ROADMAP.md`. Measurements are in `PERFORMANCE_FINDINGS.md`.

The earlier state of `codex/renderer-upstream-fixes`: the working baseline on `main` is checkpointed at `ecfdda3e`. The initial renderer backports and their binaries are committed as `e4a29ac5` and `464a6a1c`. That checkpoint includes the launcher, timing, renderer, and in-game overlay updates. The user reports it works well in gameplay.

## Implemented enhancements

- Performance work on `performance/cinematic-renderer-overhaul`:
  - **Verified in game:**
    - `tools/perf_report.py`.
    - Per-draw phase, binding and per-buffer profiling (`NHLPORT_DIAG_DRAW_PHASES=1` via `tools\run_draw_phase_profile.cmd`).
    - `vk_rendering_begins`.
    - The content-cached Utah crest check. The crest, prompts and replacements still work.
    - Pipeline cache IO-thread fixes and background driver-cache saves.
  - **Pending verification:**
    - `make_release.ps1` keeps the user's `config.json` and input mappings (`-ResetUserConfig` writes the template).
    - Profiling counters are isolated and written only while profiling, which fixes a regression of about 2 µs per draw.
  - **Rejected and reverted:** stream-buffer copy reuse (no gain).

- The launcher Controls tab selects PlayStation, Xbox or Nintendo button prompts for the next game launch. Hash-matched DDS packs load alongside existing texture overrides. Xbox and Nintendo artwork is adapted from Xelu's CC0 controller prompts. Face, shoulder, trigger, menu and selected split-mask variants are included. The Xbox pack loaded and applied several prompt textures in a release session. Controller illustrations and broad gameplay coverage remain to be verified.

- The launcher and `tools/select_button_prompts.ps1` invoke a packaged helper to remap the controls-reference font. The helper validates the EU v1.00 archive fingerprint, uses the GPL-3.0-or-later ooz decoder, records a recovery journal, and atomically replaces the TOC. Unsupported versions keep texture prompts and report the font limitation. Each Xbox or Nintendo layout change appends about 3.3 MB to the CAS archives. PlayStation restores the original TOC entries without truncating other archive data.

- Retain up to 64 MiB of otherwise-expired upload staging buffers for up to 1,800 renderer ticks. The policy applies to host upload staging and preserves GPU completion checks. Set `NHLPORT_WARM_UPLOAD_STAGING=0` to compare with the previous trimming policy. The frame CSV reports requested bytes from new and reused upload buffers, retained bytes, and CPU-side time inside `vmaCreateImage`. Isolated startup runs emitted these counters in both modes and neither retained expired blocks. The arena effect is unmeasured. The experiment was informed by [bbport's streaming measurements](https://github.com/deadinside28/bloodborne_pc/blob/master/docs/parallel_gpu.md#streaming-stutter-bb_frame_stats-stall-lines).

- Texture construction caches successful Vulkan sample-count capability queries per physical device instance, keyed by format, image type, tiling, usage, and creation flags. Unsupported format handling and fallback are preserved.

- The launcher offers persistent full-session performance logging on ordinary Play. It writes frame, thread, and stutter data automatically and uses timing-only F1 behavior. The dedicated diagnostic launch still offers full captures.

- The frame CSV records image construction separately for ordinary and expanded textures. Subtract construction time from the corresponding allocation total to estimate cache-registration time.

- Guest-to-staging reads now batch consecutive pages backed by contiguous memory into one copy. Unmapped spans remain zero-filled, and discontiguous or aliased pages still use their individual backing addresses. An isolated check matched the former implementation across 2,503 ranges. In-game timing gains are unmeasured.

- The frame CSV now separates ordinary image allocation and refresh costs from expansion. For each source-buffer path it records staging requests, guest-memory copies, staging flushes, and synchronization of GPU-backed buffers. Synchronization timing includes any copies it performs internally. The source timings are components of the corresponding buffer total.

- Diagnostic runs record CPU-side elapsed time for texture expansion in the frame CSV. The new columns distinguish image allocation and registration, source-buffer preparation, detiling, upload recording, and GPU image-copy recording. These timings describe host work and can include waits within those operations.

- Exact texture-cache lookups now check the guest address bucket first and scan the full region on a miss. Overlap resolution, image refreshes, and readback behavior are preserved.
- Diagnostic launches offer an F1 timing-only mode by default. It records a frame marker without a screenshot, draw log, or GPU image dump. Full capture is still available in the same tab.

- Added EU `CUSA37934` package detection and region-specific save lookup alongside US `CUSA37933`.
- Defaulted GPU readbacks to Relaxed to address black-ice artifacts when readbacks are disabled.
- Moved texture-cache cleanup during unmap to the GPU command thread. Added render-target resizing at reused guest addresses, bounded image copies to shared extents, preserved GPU-written subresources, and prevented stale GPU copies from overwriting newer CPU data. Mip and layer capacity are checked independently.
- Reduced redundant Vulkan pipeline binds and added descriptor update templates with a fallback to the existing descriptor-write path.
- Reused Windows waitable timers per thread, with a fallback when high-resolution timer creation is unavailable.
- Rethemed the launcher and added English controller labels, graphics presets with preview and restore, separate Vulkan driver and shader cache sizes, one-click diagnostic play, an FPS overlay toggle, and log and capture shortcuts. Launches pass the game path explicitly so the FPS flag works.
- Added the opt-in **Smooth menus & cutscenes** toggle. It lets VideoOut present on every vblank, up to the configured cap and frame availability.
- Added Menu + View (Start + Select) to open a resizable, translucent NHL settings overlay with a Sabres-inspired theme. It starts near the top left. Apply & save updates FSR, RCAS, and flip cadence during play. Display mode, present mode, HDR, frame cap, and DLSS are labeled as restart-required.
- Added a read-only Trophies tab with divided rows, centered grade icons, and distinct earned and locked labels. It reads extracted metadata and local earned state for the default emulated user. Locked hidden trophy names and descriptions stay hidden. The in-game overlay uses square corners and layered transparency, with a session text-size slider in General.
- The overlay has a live FPS toggle and exposes output dimensions, PS4 Pro mode, readbacks, and the driver cache. Applying startup-only changes prompts for a restart after saving and uses the existing emulator relaunch path.
- Overlay text sizing now applies to the parent window and child content. The in-game category row shares controller focus with the settings instead of using a separate clipped child panel.
- The overlay is freely movable and resizable with docking disabled. The Trophies tab uses focusable rows for controller navigation. It shows Platinum prerequisite progress and recognizes numeric completion and collection goals. Partial counts are marked unavailable when NHL does not report them.
- Trophy unlocks use the existing in-game notification queue, with the default toast positioned in the top-right corner.
- The FPS counter preserves settings focus when shown. The restart and save-error prompts have default controller selections.
- The launcher can rename the player 1 local profile without changing its user ID, saves, trophies, or ShadNet account settings.
- Restart now starts the replacement process before shutdown and reuses the original game path, mounts, and patch selection. If process creation fails on Windows, the game stays open and reports the error.

## Architectural decisions

- Checkpoint working changes on `codex/renderer-upstream-fixes` and push them to the configured GitHub origin. The settings, trophies, restart, and timing-marker baseline is checkpointed at `6776cd23`.

- Keep experimental renderer and launcher changes on a separate branch. Prefer general emulation fixes and preserve existing quality and compatibility defaults.
- Keep GPU-owned texture-cache mutation on the command thread. Track Vulkan pipeline binds per recording session and clear that state when host rendering bypasses it.
- Keep the native flip cadence as the default. At a 60 FPS cap, the optional override can raise presentation from 30 to as much as 60 FPS in menus and cutscenes.
- The persistent driver cache (`.vkcache`) and emulator shader and pipeline data are separate stores. This build saves driver data after startup warm-up. A small driver file does not mean the shader cache is empty.
- Build with the repository scripts and run `tools/make_release.ps1` to stage release output. Verify the deployed executables against the build outputs and preserve local game data, saves, and settings.

## Known bugs and bottlenecks

- **November 24 full-gameplay staging trace:** 22,232 frames, 16.66 ms median, and 33.40 ms p99. The extended idle-retention policy recorded zero retained bytes throughout. The 350 ms arena-loading frame spent 171 ms inside `vmaCreateImage` and 138 ms copying guest data, with no compile or readback wait. A separate 134 ms gameplay frame spent 67 ms copying guest memory into staging, with a 100 MiB request served by a new buffer. Prioritize tracing image allocations during expansion and large staging requests during play.

- **Arena image replacement:** The automatic 10,507-frame run attributed 159 ms of a 400 ms arena-loading frame to expanded-image construction and under 0.1 ms to cache registration. Sampled expansion logs show several one-mip-at-a-time chains, including 512×512 single-layer and 256×256 eight-layer images. Avoid predicting a larger guest footprint without a verified resource layout.

- **November 24 post-batching trace:** Seven timing-only markers, 16.67 ms median, 33.56 ms p99, and 83.17 ms p99.9. The largest arena-loading frame fell from 516 to 366 ms between runs. Its expansion guest-copy time fell from 163 to 108 ms, while total texture refresh volume fell from about 710 to 613 MiB. The runs differ in scene workload and starting memory headroom, so the difference does not isolate the batching gain. Expanded-image allocation and registration still took 113 ms in that frame. A later 217 ms frame recorded 104 ms of shader and pipeline compilation, and the 100 ms frame before marker 6 recorded 36 ms of readback waits.

- **November 24 source-buffer timing trace:** Seven timing markers, 16.68 ms median, and 33.59 ms p99. In the 516 ms loading frame, expansion source-buffer preparation took 167 ms, including 163 ms copying guest memory and 4 ms requesting staging space. Expanded-image allocation and registration took another 142 ms. The near-100 ms hitch before marker 2 recorded 28 ms of ordinary source-buffer preparation (17 ms guest copies and 11 ms synchronization) and 14 ms of ordinary image allocation and registration. Guest-memory copying and image churn recur in the slow frames. Staging allocation is a smaller cost. One subsequent loading frame spent 94 ms waiting on readbacks, so track that stall separately. Investigate whether fully contiguous backing pages can be copied in larger spans while preserving sparse zero-fill and memory coherency.

- **November 24 expansion timing trace:** Eight timing markers, 16.68 ms median, and 33.70 ms p99. The 400 ms loading frame recorded 106 expansions, 147 ms of expansion source-buffer preparation and 131 ms of allocation and registration. Detiling, upload recording, and image-copy recording totaled under 4 ms of host elapsed time. The 116 ms hitch before marker 3 recorded about 341 MiB of texture refreshes but only about 7 ms in the measured expansion phases. Profile source-buffer preparation and ordinary image creation and refresh separately before treating all gameplay hitches as expansion costs.

- **November 24 marker-only trace after the lookup change:** Median stayed at 16.69 ms and session p99 improved from 49.99 to 33.53 ms. F1 generated no capture files, so the changed capture mode and different session length prevent attributing the improvement to the lookup change alone. An 83.4 ms frame before marker 2 refreshed about 207 MiB of textures and waited 13.4 ms on readbacks. The largest loading hitch was 400 ms with about 573 MiB of texture refreshes, 155 image creations, 108 expansions, and under 1 ms of readback wait. No shaders or pipelines compiled in this run.

- **November 24 follow-up faceoff trace:** The same Pittsburgh–Buffalo matchup recorded 16.70 ms median and 49.99 ms p99 across the 116.7-second run, with about 17 ms of total shader and pipeline compilation. The prior half-second compile spike did not recur. The largest uncaptured loading hitch was 366 ms with 131 images created, 94 expansions, about 503 MiB of texture refreshes, and zero readback or compile wait. An uncaptured 83 ms frame approaching the opening drop refreshed about 240 MiB of textures. The 150 ms frame at the drop came immediately after an F1 dump request and is unsuitable as a clean baseline. The command processor stayed close to one logical core before the drop. Prioritize profiling texture-cache allocation and expansion during transitions and separate marker-only timing from full capture overhead.

- **November 24 EU faceoff trace:** The 82.7-second run had a 16.69 ms median and 50.05 ms p99 across loading, pregame presentation, and faceoff. Screenshots place F1 markers 2–4 at the anthem, 5 at the EA Sports transition, and 6–7 at the opening faceoff. The 533 ms loading spike recorded about 638 MiB of texture refreshes with zero compile or readback time. The 500 ms pre-faceoff spike recorded 340 ms of shader and pipeline compilation, about 227 MiB of texture refreshes, and 10.5 ms of readback wait. The GPU command processor used 93–99% of one logical core in most anthem samples. Several large spikes immediately after F1 overlap diagnostic draw and image dumps. Add a marker-only timing mode and compare repeated warmed-cache runs before changing readback behavior.

- **Intermittent Be a Pro flicker:** The EU 1.00 clip showed the 3D conversation background alternating between the expected scene and a red blur while the dialogue UI remained intact. The raw dump caught a good frame. The renderer fixes address concrete bugs, but their effect on this symptom is unverified.
- **Compilation and scene transitions:** A diagnostic run recorded a 1.83-second frame with 152 pipeline builds and a 700 ms frame with roughly 329 MiB of image uploads during a scene change. These are observed hotspots. Compare the same sequence with a warmed cache.
- **CPU headroom:** During sampled gameplay the command processor used roughly 80–93% of one logical CPU while the RTX 4060 reported roughly 37–40% busy. Gameplay was usually near the 60 FPS cap. The logs do not distinguish command processing from polling or driver-side waits.
- **Timing baseline:** Gameplay typically runs near 16.7–16.9 ms and native menus near 33.3 ms. Some gameplay windows had p99 around 34 ms. One unexplained 1.28-second gap is an isolated observation unless it recurs. No root cause has been established.
- **Memory diagnostics:** Early logs showed repeated guest mapping and protection-query errors. No link to the flicker or the timing gap has been established.
- **Launcher verification:** Recent layout and hover fixes compile and are packaged. Visual behavior at the reported Windows scaling still needs confirmation.

## Modified files and purpose

| Files | Purpose |
| --- | --- |
| `launcher/Launcher.cs` | Region detection, launcher theme and navigation, localized controls, presets, cache status, diagnostics, game arguments, and smoother menu toggle. |
| `tools/prompt-font-helper/`, `tools/select_button_prompts.ps1`, `launcher/prompt_packs/` | Reversible controls-reference font updates, standalone layout selection, and controller artwork. |
| `src/core/emulator_settings.h`, `src/core/libraries/videoout/driver.cpp` | Relaxed readback default, optional flip-rate setting, and VideoOut presentation cadence. |
| `src/common/thread.cpp` | Per-thread Windows sleep timer reuse and fallback. |
| `src/sdl_window.cpp`, `src/imgui/big_picture/settings_dialog_imgui.cpp`, `settings_dialog_imgui.h` | Controller shortcut, compact in-game layout, and additional settings controls. |
| `src/video_core/renderer_vulkan/vk_runtime.cpp`, `src/video_core/texture_cache/texture_cache.cpp`, `src/video_core/texture_cache/types.h` | Render-target resizing, image-copy bounds, texture data coherency, and independent mip/layer checks. |
| `src/video_core/renderer_vulkan/vk_scheduler.cpp`, `vk_scheduler.h`, `vk_presenter.cpp`, `vk_rasterizer.cpp` | Command-session bind tracking, host-pass invalidation, and GPU-thread texture unmap. |
| `src/video_core/buffer_cache/fault_manager.cpp`, `src/video_core/texture_cache/blit_helper.cpp`, `tile_manager.cpp` | Route helper pipeline binds through scheduler tracking. |
| `src/video_core/renderer_vulkan/vk_compute_pipeline.cpp`, `vk_graphics_pipeline.cpp`, `vk_pipeline_common.cpp`, `vk_pipeline_common.h` | Create and use Vulkan descriptor update templates with the descriptor-write fallback. |
| `game_patches/nhl24.xml`, `release/user/patches/nhl24.xml` | Source and packaged patch files with EU support noted. |
| `README.md`, `release/README.txt`, `tools/make_release.ps1` | Player instructions and local release assembly. |
| `release/user/config.json`, `release/nhl24.exe`, `release/launcher.exe` | Local release settings and rebuilt executables. |
| `PROJECT_STATUS.md` | Branch, implementation, verification, and outstanding work. |

## Pending development

- Verify the remapped controls-reference font in-game across Xbox and Nintendo layouts. The archive helper is verified against EU v1.00 data and safely declines unsupported fingerprints. Test US and other versions before extending support.
- Verify Xbox and Nintendo prompts across game versions, gameplay contexts and controller configurations. Controller illustrations and some control-layout images are not covered by the initial packs. The texture override skips arrays and render targets, and synchronizes the GPU for matching upload candidates. Measure its performance impact.
- Compare cold and warm upload staging. [bbport's streaming trace](https://github.com/deadinside28/bloodborne_pc/blob/master/docs/parallel_gpu.md#streaming-stutter-bb_frame_stats-stall-lines) found CPU first-touch costs when staging pages were recreated between bursts. The latest NHL arena hitch recorded about 151,000 process-wide page faults, which cannot identify the faulting allocation. Measure new versus recycled upload blocks and test bounded retention before moving copies onto workers.
- Check [shadPS4 #5761](https://github.com/shadps4-emu/shadPS4/pull/5761) against NHL equal-address texture requests with the same layout and different guest byte sizes. Track matches before backporting. The fix addresses a one-mip, one-layer case and the observed mip-growth chains require different handling.
- Evaluate [shadPS4 #5711](https://github.com/shadps4-emu/shadPS4/pull/5711) separately for Windows startup commit pressure. The author tested it on NHL 19 with unchanged frame rate. Keep any NHL 24 trial isolated from release builds until mapping and startup behavior are verified.

- EU trophy metadata loads locally. The game archive extracted 59 files for `NPWR34955_00`, including 51 trophy definitions. Verify a real in-game unlock persists across restarts before adding a launcher trophy view. Keep local keys and extracted game metadata out of version control.
- Verify the experimental build in EU and US gameplay, especially Be a Pro conversations, faceoffs, replays, and cutscenes. Compare the smoother menu toggle on and off for frame delivery and audio sync.
- Verify Menu + View on a physical controller, gamepad navigation, overlay readability at smaller window sizes, and applying and saving the added settings.
- Verify an overlay-triggered restart preserves the game path, patch selection, saved graphics settings, and diagnostic environment in a real session.
- Benchmark repeated warmed-cache runs against the main checkpoint. Compare p99 frame time and command-processor CPU time before attributing gains to binding, descriptor, or timer changes.
- Profile the command processor with Windows CPU sampling. Collect wait stacks if a long unexplained pause recurs. If graphics corruption recurs, capture a bad frame and compare the scene before and after UI composition.
- Investigate why the Vulkan driver cache stays small and evaluate a safe save point after gameplay. Keep the existing shader and pipeline data intact during comparisons.
- Update this file after substantive changes, release builds, or new diagnostic findings. Record the measured result, the branch or build tested, and remaining verification.
