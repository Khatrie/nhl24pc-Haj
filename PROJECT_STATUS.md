# NHL24 PC project status

Updated November 23, 2026. Active branch: `codex/renderer-upstream-fixes`. The working baseline on `main` is checkpointed at `ecfdda3e`. The initial renderer backports and their binaries are committed as `e4a29ac5` and `464a6a1c`. The current checkpoint includes the launcher, timing, renderer, and in-game overlay updates. The Windows Release build and launcher rebuild succeeded, and both local release executables were verified against their build outputs. The user reports the branch is working well in gameplay. Performance gains have not been benchmarked.

## Implemented enhancements

- Added EU `CUSA37934` package detection and region-specific save lookup alongside US `CUSA37933`.
- Defaulted GPU readbacks to Relaxed to address black-ice artifacts when readbacks are disabled.
- Moved texture-cache cleanup during unmap to the GPU command thread. Added render-target resizing at reused guest addresses, bounded image copies to shared extents, preserved GPU-written subresources, and prevented stale GPU copies from overwriting newer CPU data. Mip and layer capacity are checked independently.
- Reduced redundant Vulkan pipeline binds and added descriptor update templates with a fallback to the existing descriptor-write path.
- Reused Windows waitable timers per thread, with a fallback when high-resolution timer creation is unavailable.
- Rethemed the launcher and added English controller labels, graphics presets with preview and restore, separate Vulkan driver and shader cache sizes, one-click diagnostic play, an FPS overlay toggle, and log and capture shortcuts. Launches pass the game path explicitly so the FPS flag works.
- Added the opt-in **Smooth menus & cutscenes** toggle. It lets VideoOut present on every vblank, up to the configured cap and frame availability.
- Added Menu + View (Start + Select) to open a translucent NHL settings overlay at the top left, up to 560 × 420 pixels. The in-game view shows General, Graphics, and Input. Frame-cap and DLSS changes are labeled as restart-required.

## Architectural decisions

- Keep experimental renderer and launcher changes on a separate branch. Prefer general emulation fixes and preserve existing quality and compatibility defaults.
- Keep GPU-owned texture-cache mutation on the command thread. Track Vulkan pipeline binds per recording session and clear that state when host rendering bypasses it.
- Keep the native flip cadence as the default. At a 60 FPS cap, the optional override can raise presentation from 30 to as much as 60 FPS in menus and cutscenes.
- The persistent driver cache (`.vkcache`) and emulator shader and pipeline data are separate stores. This build saves driver data after startup warm-up. A small driver file does not mean the shader cache is empty.
- Build with the repository scripts and run `tools/make_release.ps1` to stage release output. Verify the deployed executables against the build outputs and preserve local game data, saves, and settings.

## Known bugs and bottlenecks

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

- Verify the experimental build in EU and US gameplay, especially Be a Pro conversations, faceoffs, replays, and cutscenes. Compare the smoother menu toggle on and off for frame delivery and audio sync.
- Verify Menu + View on a physical controller, gamepad navigation, overlay readability at smaller window sizes, and applying and saving the added settings.
- Benchmark repeated warmed-cache runs against the main checkpoint. Compare p99 frame time and command-processor CPU time before attributing gains to binding, descriptor, or timer changes.
- Profile the command processor with Windows CPU sampling. Collect wait stacks if a long unexplained pause recurs. If graphics corruption recurs, capture a bad frame and compare the scene before and after UI composition.
- Investigate why the Vulkan driver cache stays small and evaluate a safe save point after gameplay. Keep the existing shader and pipeline data intact during comparisons.
- Update this file after substantive changes, release builds, or new diagnostic findings. Record the measured result, the branch or build tested, and remaining verification.
