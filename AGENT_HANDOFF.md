# Agent handoff

Updated November 27, 2026, 3:20 PM ET. Read this first, then `PERFORMANCE_FINDINGS.md` (evidence) and `PERFORMANCE_ROADMAP.md` (plan). Do not restart the investigation. The baseline, tooling and bottleneck attribution are done.

## 1. Where things stand

- **Working copy:** `C:\github\nhl24pc-hilals` on the owner's Windows machine. It is the source of truth, and the remote may lag.
- **Branch:** `performance/cinematic-renderer-overhaul`, created from `codex/renderer-upstream-fixes` at `d2d799b7`. It is **not pushed**, and nothing is merged into `main`.
- **Owner:** plays the game and runs builds on request. Hardware: Ryzen 7 7435HS (8C/16T), RTX 4060 Laptop, 16 GB RAM, Windows 11.
- **Do not touch:**
  - `release/user/config.json` and `release/user/input_config/*.ini` (owner settings).
  - Untracked research folders under `tools/` (`HockeyModdingSuite-research`, `hms-investigation`, `ooz-research`, `xelu-prompt-research`).
  - Game data, saves and the shader cache.

### Commits on the branch (oldest first)

| Commit | Change | State |
|---|---|---|
| `b5027360` | Per-draw phase timers (`NHLPORT_DIAG_DRAW_PHASES=1`), `vk_rendering_begins` counter, `tools/perf_report.py` | Verified working in game |
| `40e2227b` | Pipeline cache IO thread drains its queue on shutdown; atomic request counter; driver `.vkcache` autosaved on a worker 10 s after runtime compiles stop (`NHLPORT_DRIVER_CACHE_AUTOSAVE=0` disables) | Verified: save logged at 45.9 MB in 46 ms on `shadPS4:DriverCacheSave` |
| `49c5e93f` | Array-blank (Utah crest) check caches layer hashes by guest content; skips the GPU download and `Finish()` on repeats | Owner confirmed crest hiding, prompts and replacements still work; no measurable FPS change |
| `091a7bfb` | Performance docs | — |
| `676e4239` | `tools/run_draw_phase_profile.cmd` (sets `NHLPORT_DIAG_DRAW_PHASES=1`, starts the launcher) | Works |
| `7944dcfe` | Resource-binding sub-phase timers and per-draw cleanup phase | **Introduced a regression of about 2 µs per draw with profiling off**; fixed by `c6e0fe98` |
| `592f95dd` | `make_release.ps1` keeps the existing user `config.json` and input `.ini` files (`-ResetUserConfig` restores the template) | Not yet exercised by the owner |
| `ab71997d`, `3113db94` | Stream-buffer copy reuse experiment and its launch script | Rejected; reverted by `a887b745` and `15b55245` |
| `c6e0fe98` | Profiling counters moved to a cache-aligned `Diag::C::draw_profile` block, written only while profiling | **Needs verification run 1 (below)** |
| `84f38682` | Per-buffer step timers (sharp, stream/cached obtain, GPU invalidate, access check, special) | **Needs verification run 2 (below)** |

### Staged binaries

`release\nhl24.exe` and `build\nhl24.exe` date from 2:54 PM, before `c6e0fe98`. **Rebuild before the next test.**

## 2. What was measured

All numbers come from real owner sessions; nothing is estimated. Details are in `PERFORMANCE_FINDINGS.md`.

- **Cinematic slowdowns come from draw-call throughput on the GPU command processor thread.**
  - FPS falls from about 59 to 35–45 once draws exceed about 60k/s.
  - The thread runs at 90–98% of one core while the GPU is 40–45% busy.
  - Compilation and file I/O are not the recurring cause.
- **Cost is about 11.4 µs of command-thread time per draw** (baseline, profiling off).
- **Phase split with profiling on, slow windows:**

  | Phase | µs/draw |
  |---|---:|
  | Resource binding | 7.7 |
  | — buffers, about 12 per draw | 5.5 |
  | — texture lookup | 1.3 |
  | — texture views | 0.8 |
  | — samplers | 0.1 |
  | Pipeline lookup | 1.6 |
  | Descriptor push | 1.2 |
  | Command recording | 0.9 |
  | Per-draw cleanup | 0.6 |
  | Vertex/index binding | 0.6 |

  These totals include about 2 µs of the now-fixed counter regression and roughly 0.8 µs of timer overhead.
- **Render-pass churn is ruled out:** only 0.09 real begins per draw.
- **Small read-only buffers are re-copied into the stream buffer on every draw:** about 15 KiB per draw, around 1 GiB/s in cinematics. Bytes are not the cost; reuse saved nothing.
- **Buffer readbacks are a secondary stall source:** 26–36 ms per second of GPU-wait on the command thread.

## 3. Immediate next actions

1. **Owner rebuilds:** `tools\build_nhlport.ps1`, then `tools\make_release.ps1`. Check that the script prints "Keeping existing …config.json".
2. **Verification run 1 (profiling off):** normal launcher, the usual Be a Pro sequence with F1 markers. Archive the run to `release/user/log/<name>/` before the next run overwrites it. Expected result: median command-thread cost per draw is back to about 11.4 µs. Compute it with the per-draw snippet in §5.
3. **Verification run 2 (profiling on):** launch with `tools\run_draw_phase_profile.cmd`. Confirm `NHLPORT_DIAG_DRAW_PHASES=1` appears in the header of `nhl24_diag.log`. The report's "Buffer binding steps" table says which step owns the 5.5 µs.
4. **Optimize the dominant buffer step:**
   - **If it is `obtain_stream`:** cut per-buffer overhead in the stream path of `BufferCache::ObtainBuffer`: the region check, `Map`/`Commit` bookkeeping and the sparse copy setup. A plan to batch per stage is in the roadmap.
   - **If it is `obtain_cached`:** look at `SynchronizeMemory`/`EnsureResident` work for unchanged buffers.
   - **If it is `gpu_inval`:** skip `InvalidateMemoryFromGPU` when no image is registered in the range, or the range was already invalidated this tick.
   - **If it is `sharp`:** cache V# reads per stage.
5. **Then the next phases by size:** pipeline lookup (1.6 µs; per-stage specialization cache), descriptor push (1.2 µs; skip unchanged pushes) and texture lookup (1.3 µs; T#-keyed cache).

## 4. Testing protocol

- **Settings:** FIFO, 1920×1080 windowed, RCAS on. These are now stable because `make_release` keeps `config.json`. Warn the owner if the `# config:` line in `nhl24_diag.log` changes between runs.
- **Launching:**
  - Profiling runs must go through `tools\run_draw_phase_profile.cmd`. The launcher sets only its own variables, and an ordinary launch silently drops `NHLPORT_DIAG_DRAW_PHASES`.
  - Always check the `# environment overrides:` line.
  - The `# port:` line names the build that actually ran. Check it: once, an unstaged build ran the old exe.
- **Scene:** Be a Pro with arena load, anthem, faceoff, goals and replays, and F1 at the start of each cinematic.
- **Analysis:** run `python tools/perf_report.py <log dir> [--compare <dir>]`.
  - `--compare` compares raw totals and is misleading for sessions of different length.
  - Compare per-second rates, or command-thread µs per draw, instead.
- **Archive every run.** Existing archives in `release/user/log/`:

  | Folder | Build | Notes |
  |---|---|---|
  | `baseline 11-26-26` | `d6fdc8bf` | 82 min, fullscreen 4K window, FIFO |
  | `checkpoint1 11-27-26` | `091a7bfb` | Profiling off |
  | `phases1 11-27-26` | `091a7bfb` | Profiling on, Mailbox |
  | `subphase-off 11-27-26` | `7944dcfe` | Profiling off; shows the regression |
  | `dedup 11-27-26` | `3113db94` | Profiling plus stream reuse |
  | `phases2 11-27-26` | `3113db94` | Profiling on |

## 5. Environment notes for agents

- **The device shell (`device_bash`) is a Linux VM, not Windows.** It cannot run clang-cl, MSVC, PowerShell or the game. Windows builds are done by the owner.
- **Git in the VM:**
  - Pass `-c core.autocrlf=true` on every git command. The owner's global setting is invisible to the VM; without it, every file looks modified and `git am` fails on CRLF files.
  - Set `GIT_COMMITTER_NAME=Khatrie` and `GIT_COMMITTER_EMAIL=Fire666dev@gmail.com` (author too, for new commits).
  - Prefer `git status --untracked-files=no --ignore-submodules=all -- <paths>`. A full status over the mount exceeds the timeout.
  - Git needs delete permission in the repo folder for its lock files; request it once per session.
- **Cloud compile check:**
  - Source: clone `Khatrie/nhl24pc-Haj`, apply the branch patches, and initialize submodules with `--depth 1`.
  - fdk-aac is unreachable from the cloud (googlesource is blocked); copy it from the owner's `externals/aacdec/fdk-aac`.
  - Configure with clang, Ninja, Release, `-DFETCHCONTENT_SOURCE_DIR_FMT=<repo>/externals/fmt -DSDL_WAYLAND=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_IBUS=OFF -DENABLE_DISCORD_RPC=OFF -DENABLE_UPDATER=OFF`. Install the X11, ALSA, Pulse and udev dev packages.
  - Quick check: `ninja -t commands <obj>`, then rerun that command with `-fsyntax-only`.
- **Moving changes to the owner:**
  - `git format-patch` in the cloud.
  - Copy the patch into `build\` on the device.
  - `git -c core.autocrlf=true am --committer-date-is-author-date`, then delete the patch.
  - Verify that `git rev-parse HEAD:src` matches on both sides.
- **Per-draw cost snippet:** for each one-second window with at least 40k draws, compute `GpuCommandProcessor cpu_pct × 10000 / (draws + dispatches)` from `nhl24_threads.csv` and `nhl24_frames.csv`, then take the median. This is the most robust cross-run metric.
- **Windows Error Reporting:** access to `C:\ProgramData\Microsoft\Windows\WER` was refused. Ask the owner to read crash details from Reliability Monitor.

## 6. Open risks and unfixed issues

- **One unexplained crash to desktop** at 106 s on build `091a7bfb`, after a switch into a 2112×2112 scene. There was no dump, no log error and no memory pressure. It has not recurred in four runs. If it repeats, get the exception code and faulting module from Reliability Monitor.
- **Startup warm-up once took 34.7 s instead of 4.7 s** with the same cache; probably the NVIDIA driver's own cache. Watch for it.
- **Pipeline cache identity issues (unfixed; each fix forces one full recompile through a version bump):**
  - Permutation index depends on visit order.
  - Preload omits the clip-distance discard fragment shader (`SerializationSupport` does not serialize `fragment`; preload passes stale `runtime_infos`).
- **Driver cache autosave:** `getPipelineCacheData` may briefly hold the driver's cache lock. Save time is logged; disable with `NHLPORT_DRIVER_CACHE_AUTOSAVE=0` if hitches appear around saves.
- **Array-blank cache correctness assumption:** an uploaded image is a pure function of the guest bytes and the layout when not GPU-modified, and the hash is taken before and after the upload. Debug with `NHLPORT_ARRAY_LOG=1` (`ARRAYHASH` lines).
