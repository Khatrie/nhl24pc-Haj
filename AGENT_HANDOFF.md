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
| `882d2252` | **Stacked logs:** every run appends to the same `nhl24_frames.csv` / `nhl24_threads.csv` / `nhl24_diag.log`; `session` column + `# ===== session <id> =====` marker; `tools/diag_sessions.py`; `perf_report.py --list-sessions / --session / --trend`; `diag_report.py --session` | Helpers tested in a stub build and on real logs; first in-game run pending |

### Ported from HajDEO/nhl24pc (remote `hajdeo`, compared Nov 27)

| Commit | Change | Their commit |
|---|---|---|
| `cfd2219f` | Red-zone protection covers 246 more eboot instructions (random `0xc0000005` at `eboot+0x666096a`) and defaults on, with the launcher toggle defaulting to on. Also a save-data fix: unclean exit no longer wipes `param.sfo` ("vanishing rosters"). | `11d63557` |
| `cd5d203a` | Incompatible pipeline cache is rebuilt instead of ignored forever. The old cache is kept as `<cache>.old`. | `b3e1c572` |
| `b84c7935` | Driver-internal C++ exceptions no longer treated as crashes. | `b8d98a55` |
| `6180ccfc` | Texture cache frees idle superseded aliases (VRAM exhaustion on 8 GB cards); GC counts only real deletions. | `6b08321a` (part) |

**Not ported:**
- Intel/AMD-only fixes: `006cf04e`, `380e0fa1`, `51a6f1ae`, `d83ec81f`.
- Linux build and CI.
- `Input.merge_controllers`.
- The dynamic-resolution image resize; we disable dynamic resolution by game patch instead.
- **Pending its own checkpoint:** the shared-memory barrier pass for split GCN waves from `6b08321a`. It changes NVIDIA compute shaders (32-wide subgroups) and needs a shader cache version bump, which means a one-time full recompile.

Red-zone protection is always on since `adb7ff53`. The emulator ignores `redzone_patches`, both toggles are gone, and the launcher drops the old key on save.

### Staged binaries

`release\nhl24.exe` and `release\launcher.exe` match `build\` byte for byte, built 5:00 PM Nov 27 from `adb7ff53` with `tools\build_and_stage.cmd`, which kept `config.json` and the input `.ini` files and builds the launcher too. Close the launcher before staging.

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

**Verification runs done (Nov 27, build `fca8b5eb`; both stacked in `release/user/log`).**

- **Run `20261127-154844`, profiling off:** 11.54 µs/draw. The counter regression is gone.
- **Run `20261127-155502`, profiling on:** 15.87 µs/draw. Profiling adds about 4 µs/draw, enough to drop mid-ice to about 47 FPS; this is expected.
- **Buffer steps, slow windows:**

  | Step | µs/draw | µs per buffer |
  |---|---:|---:|
  | `obtain_stream` | 4.06 | 0.62 |
  | `obtain_cached` | 1.68 | 0.36 |
  | sharp, GPU invalidate, access check | about 0.3 each | |

  There are about 6 stream and 4.2 cached buffers per draw.
- **Regression over 164 one-second windows:** `obtain_stream` costs 0.39 µs fixed per buffer plus 0.071 µs per KiB (R² 0.98, about 3.2 KiB per buffer). The fixed part dominates.
  - Most likely cause: cache misses on the per-buffer metadata (tracker, page table, source line, ring), not the copy itself.
  - This is why the earlier hash-based reuse saved nothing: it still read the guest memory.

1. **Stream-copy reuse (`30fd8026`): rejected and reverted.** See `PERFORMANCE_FINDINGS.md`. Only 10% of binds reused, the run was 14.19 vs 11.54 µs/draw, and the scheme is unsound against direct guest CPU writes.
2. **Measurement note:** there is one profiling-off run per build so far, so run-to-run variance is not established. Do not credit or blame a change for less than about 1 µs/draw until two runs of the same build bound the noise.
3. **Then the next phases by size:** pipeline lookup (1.7 µs; per-stage specialization cache), descriptor push (1.2 µs; skip unchanged pushes), texture lookup and views (2.1 µs; T#-keyed cache), and `obtain_cached` (1.7 µs).

## 4. Testing protocol

- **Settings:** FIFO, 1920×1080 windowed, RCAS on. These are now stable because `make_release` keeps `config.json`. Warn the owner if the `# config:` line in `nhl24_diag.log` changes between runs.
- **Launching:**
  - Profiling runs must go through `tools\run_draw_phase_profile.cmd`. The launcher sets only its own variables, and an ordinary launch silently drops `NHLPORT_DIAG_DRAW_PHASES`.
  - Always check the `# environment overrides:` line.
  - The `# port:` line names the build that actually ran. Check it: once, an unstaged build ran the old exe.
- **Scene:** Be a Pro with arena load, anthem, faceoff, goals and replays, and F1 at the start of each cinematic.
- **Logs stack across runs** (since the stacked-logs commit). The game appends every run to `release/user/log/nhl24_frames.csv`, `nhl24_threads.csv` and `nhl24_diag.log`:
  - Every CSV row starts with a `session` column (the run's local start time, e.g. `20261127-153100`). The diag log starts each run with `# ===== session <id> =====`.
  - When a build changes the CSV columns, or a file passes 1 GiB (`NHLPORT_DIAG_STACK_MB`), the old file is renamed `nhl24_frames.until-<id>.csv` and a new one starts. Nothing is deleted.
  - `NHLPORT_DIAG_APPEND=0` restores one fresh set per run.
  - Pre-stacking loose logs are moved aside the same way on the first run of the new build.
- **Analysis:**
  - `python tools/perf_report.py release/user/log --list-sessions` lists runs, with their build and env overrides.
  - `python tools/perf_report.py release/user/log --trend` prints one row per run: FPS, P99, sustained-slow seconds and **CP µs/draw** (the bottleneck metric, computed automatically). Use it to track builds over many sessions.
  - `python tools/perf_report.py release/user/log [--session latest|all|last:N|<id or unique part>]` gives the full report. It defaults to the latest run.
  - `--compare <dir> --compare-session <sel>` compares two runs, including two runs in the same stack. Raw totals mislead for runs of different length, so prefer CP µs/draw (now in the comparison table) and per-second rates.
  - `diag_report.py` takes the same `--session` and `--list-sessions`.
- **Older per-run archives** in `release/user/log/` still work with every tool (they read as session `-`):

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
