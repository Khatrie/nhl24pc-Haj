# Agent handoff

Updated November 27, 2026, 6:05 PM ET. Read this first, then `PERFORMANCE_FINDINGS.md` (evidence) and `PERFORMANCE_ROADMAP.md` (plan, including the proposed threading work). Do not restart the investigation: the baseline, the noise level, the profiler and the bottleneck attribution are done.

## 1. Where things stand

- **Working copy:** `C:\github\nhl24pc-hilals` on the owner's Windows machine. It is the source of truth.
- **Branch:** `performance/cinematic-renderer-overhaul`, from `codex/renderer-upstream-fixes` at `d2d799b7`. HEAD is `d2ca6b51` plus these doc updates.
  - **Not pushed:** an `add_repo` push grant was denied by a permission check. The owner can run `git push -u origin performance/cinematic-renderer-overhaul`.
  - Nothing is merged into `main`.
- **Remotes:**
  - `origin`: Khatrie/nhl24pc-Haj.
  - `upstream`: hilals/nhl24pc.
  - `hajdeo`: HajDEO/nhl24pc, fetch only, used for the port comparison.
- **Owner:** plays the game and reports when a run is done. Hardware: Ryzen 7 7435HS (8C/16T), RTX 4060 Laptop with 8 GB, 16 GB RAM, Windows 11.
  - The owner cares about cinematic FPS drops. Visual glitches have not been a problem; still check them after texture or binding changes.
- **The agent builds and stages itself.** It double-clicks `tools\build_and_stage.cmd` in File Explorer through computer use, which can only click in Explorer.
  - The script builds `nhl24.exe` and `launcher.exe` and runs `make_release.ps1`, which keeps `config.json` and the input `.ini` files.
  - Progress goes to `build\build_and_stage.log`; `build\build_and_stage.done` holds the exit code.
  - **Close the launcher first**, because staging overwrites `release\launcher.exe`.
  - The owner authorized closing the game and launcher for builds.
- **Do not touch:**
  - `release/user/config.json` and `release/user/input_config/*.ini`.
  - Untracked research folders under `tools/`.
  - Game data, saves and the shader cache.

### Staged build

`release\nhl24.exe` = `build\nhl24.exe` (SHA-256 `ac8bd531…`), built 5:39 PM Nov 27 from source `d2ca6b51`.
- Its `# port:` line reads `cb799cc4-dirty`, because the commit was made after the build.
- It contains the CP sampler and optimizations A and B, all off by default.
- `build\nhl24.map` belongs to this build.

### Commits since the November 27 morning checkpoint (oldest first)

| Commit | Change | State |
|---|---|---|
| `882d2252` | Stacked diagnostic logs (`session` column, session markers, `tools/diag_sessions.py`, `perf_report.py --trend / --session / --list-sessions`) | Verified in game; old loose logs were moved aside as `*.until-*` |
| `30fd8026` → `8fdf1839` | Stream-copy reuse without hashing, then its revert | Rejected (see findings) |
| `cfd2219f`, `adb7ff53` | Red-zone crash fix and save-data fix ported from HajDEO v0.9.3. Red-zone protection is **always on**; the launcher and menu toggles are removed. | Run `20261127-171410` ended cleanly, no crash |
| `cd5d203a` | Incompatible pipeline cache is rebuilt; the old one is kept as `<cache>.old` (from HajDEO `b3e1c572`) | Not triggered yet |
| `b84c7935` | Host C++ exceptions are not treated as crashes (from HajDEO `b8d98a55`) | — |
| `6180ccfc` | Texture cache frees idle superseded aliases, for VRAM on 8 GB cards; the GC counts only real deletions (from HajDEO `6b08321a`, part) | Ran cleanly; no visual problems reported |
| `93cac446` | `build_and_stage.cmd` also builds the launcher | Works |
| `cb799cc4` | **GPU command-thread sampling profiler** (`NHLPORT_CP_SAMPLE=1`, `tools/run_cp_profile.cmd`, `tools/cp_profile.py`) and the linker map `build/nhl24.map` | Verified: 98,668 samples, 1.7% suspension |
| `d2ca6b51` | Exact optimizations A (`NHLPORT_OPT_STREAM_UNTRACKED=1`) and B (`NHLPORT_OPT_TEX_INFO_MEMO=1`); launchers `tools/run_opt_a.cmd`, `tools/run_opt_ab.cmd` | Measured: within noise (see §2) |

Earlier commits (profiling timers, pipeline-cache drain, crest cache, `make_release` keeping settings) are listed in `PERFORMANCE_ROADMAP.md` under Done.

## 2. What was measured

All numbers come from real owner sessions, stacked in `release/user/log`. Get them with `python tools/perf_report.py release/user/log --trend`.

| Session | Source | Switches | Avg FPS | CP µs/draw | Notes |
|---|---|---|---:|---:|---|
| `20261127-154844` | `fca8b5eb` | none | 57.6 | 11.54 | Baseline |
| `20261127-155502` | `fca8b5eb` | `DIAG_DRAW_PHASES` | 51.3 | 15.87 | Timers add about 4 µs/draw |
| `20261127-161744` | `30fd8026` | `STREAM_REUSE` | 53.0 | 14.19 | Rejected |
| `20261127-171410` | `adb7ff53` | none | 54.7 | 11.40 | Baseline repeat; with the ports |
| `20261127-172849` | `cb799cc4` | `CP_SAMPLE` | 53.8 | 12.22 | Sampler costs about 0.8 µs/draw |
| `20261127-174246` | `d2ca6b51` | `OPT_STREAM_UNTRACKED` | 56.1 | 11.65 | A: no gain |
| `20261127-174648` | `d2ca6b51` | A + `OPT_TEX_INFO_MEMO` | 56.6 | 11.24 | A+B: about 2%, at the noise limit |

- **Noise:** two baseline runs of the same draw path differ by 0.14 µs/draw. Treat less than about 0.3 µs as noise until more repeats exist.
- **FPS and slow seconds vary much more with scene mix**, meaning how many cinematics a run contains. CP µs/draw is the comparison metric.
- **Target:** cinematics hold 60 FPS only at about 4.6 µs/draw, against 11.4 now.
- **Where the time goes:** the profile summary, with stacks, is in `PERFORMANCE_FINDINGS.md` ("Command-thread sampling profile").

## 3. Immediate next actions

1. **Decide on A+B.** Both are exact, and the best run used them. Making them default on is harmless but worth at most 1–2%. Do it together with the next build.
2. **The owner was offered the threading plan** (`PERFORMANCE_ROADMAP.md`, "Proposed: multi-threaded command processing"). It has 3 threads, in stages T0 → T1 → T2, each behind a switch and measured with the owner. Wait for the owner's go-ahead before starting T1, which is a multi-day change. T0 is small and can start once approved.
3. **Pending small items:**
   - Shared-memory barrier port (needs a shader cache version bump and a one-time recompile).
   - Pipeline cache identity fixes, which can share the same version bump.
   - Pushing the branch.

## 4. Testing protocol

- **Settings:** FIFO, 1920×1080 windowed, RCAS on, `ignore_flip_rate` true. Check the `# config:` line is `same` in `--trend`.
- **Launching:** use the `tools\run_*.cmd` scripts to set switches. The launcher passes its environment to the game.
  - Always check the `# environment overrides:` line.
  - The `# port:` line names the build that ran.
- **Scene:** Be a Pro, including arena load, anthem, faceoff, goals and replays. Make sure several cinematics are in each run. F1 markers are optional.
- **Quitting:** the owner quits with Alt+F4. That is a clean shutdown, so the logs and the sampler file are written.
- **One request per decision:** batch runs into one message, for example a control run and an experiment run. The owner only plays and says "done".
- **Profiling:**
  - `tools\run_cp_profile.cmd` runs the sampler.
  - Analyse with `python tools/cp_profile.py release/user/log --map build/nhl24.map`. Run it in the cloud container for `llvm-undname`, or on the device with rough demangling.
  - The map must come from the build that ran; the next build overwrites it.
- **Logs stack across runs:**
  - A CSV header change or the 1 GiB cap moves the old file aside as `*.until-<id>.*`.
  - `NHLPORT_DIAG_APPEND=0` gives fresh files per run.
  - Older per-run archive folders still work (they read as session `-`).

## 5. Environment notes for agents

- **The device shell (`device_bash`) is a Linux VM**, with the repo mounted at `$HOME/mnt/nhl24pc-hilals`. It cannot run Windows tools; builds go through `build_and_stage.cmd`, as above.
- **Git in the VM:**
  - Use `-c core.autocrlf=true` on every command.
  - Set author and committer to `Khatrie` / `Fire666dev@gmail.com`.
  - Give status and diff explicit paths, because a repo-wide scan over the mount times out.
  - Git needs delete permission in the repo folder for its lock files; request it once per session.
- **Editing CRLF files:** read the bytes, replace `\r\n` with `\n`, edit, and write back with `\r\n`.
  - Apply upstream patches with `git apply --cached`, then `git checkout -- <files>`.
  - Write patch scripts to the cloud container, `device_commit_files` them into `build\`, run them, and delete them.
- **Per-draw cost:** `perf_report.py` computes CP µs/draw: the median over seconds with at least 40k draws of `GpuCommandProcessor cpu_pct × 10000 / (draws + dispatches)`.
- **Windows Error Reporting** is not readable; ask the owner for Reliability Monitor details if a crash happens.

## 6. Open risks and unfixed issues

- **Earlier unexplained crash to desktop** (106 s, build `091a7bfb`, 2112×2112 scene): it has not recurred. The red-zone fix may cover it.
- **Pipeline cache identity issues** (each fix forces one full recompile through a version bump):
  - The permutation index depends on visit order.
  - Preload omits the clip-distance discard fragment shader.
- **Driver cache autosave** may briefly hold the driver's cache lock. Disable with `NHLPORT_DRIVER_CACHE_AUTOSAVE=0` if hitches appear around saves.
- **The sampler** suspends the command thread at 500 Hz while it is on. Unwinding is lock-free, but use it only for profiling runs.
