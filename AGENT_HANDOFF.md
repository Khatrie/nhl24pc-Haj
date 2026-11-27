# Agent handoff

## Where things stand

- **Working copy:** `C:\github\nhl24pc-hilals` (owner's machine; this is the source of truth).
- **Branch:** `performance/cinematic-renderer-overhaul`, created from `codex/renderer-upstream-fixes` at `d2d799b7`. It has not been pushed.
- **Baseline:** the 82-minute session in `release/user/log` from 2026-11-26, build `d6fdc8bf-dirty`. That build differs from `d2d799b7` only in `texture_cache.cpp` (the prompt-pack commit). Its summary was produced with `tools/perf_report.py`.
- **Untracked local research folders** under `tools/`: `HockeyModdingSuite-research`, `hms-investigation`, `ooz-research`, `xelu-prompt-research`. Leave them alone.
- **Locally modified, not ours:** `release/user/config.json` and `release/user/input_config/default.ini` are the owner's settings. Never commit or revert them.

## Checkpoint 1 contents

See the table in `PERFORMANCE_ROADMAP.md`. In short:
- draw-phase timers
- the `vk_rendering_begins` counter
- the array-blank content-hash cache
- the pipeline cache IO-thread fix
- the background driver-cache save
- `tools/perf_report.py`

## Build status

- **Linux syntax check:** every changed translation unit passed `clang++-18 -fsyntax-only` against a configured tree in the cloud container.
  - That tree is at `/home/claude/build`, with `-DFETCHCONTENT_SOURCE_DIR_FMT=externals/fmt` and fdk-aac copied in from the owner's checkout.
  - The container cannot reach `android.googlesource.com` or GitHub archive downloads.
- **Windows (clang-cl, `tools/build_nhlport.ps1`):** not yet built. This must be done on the owner's machine. The cloud workspace's device shell is a Linux VM and cannot run MSVC/clang-cl.

## Immediate next action

1. The owner builds the Windows Release and runs the checkpoint 1 gameplay test (`PERFORMANCE_ROADMAP.md`).
2. Run `python tools/perf_report.py <new log dir> --compare <baseline log dir>`. Read:
   - the "GPU command thread time per draw" table
   - "Render passes actually begun"
3. Pick roadmap item 1, 2 or 3 based on which phase dominates.

## Open risks

- **Array-blank cache:** it relies on the refreshed image content being a pure function of the guest bytes and the layout whenever the guest range is not GPU-modified. If crest hiding ever fails on Utah jerseys, set `NHLPORT_ARRAY_LOG=1` and compare `ARRAYHASH` lines.
- **Driver-cache autosave:** it writes `<serial>.vkcache.tmp`, then renames it. Disable it with `NHLPORT_DRIVER_CACHE_AUTOSAVE=0` if any save-related hitch shows up.
- **Pipeline-key churn and the missing discard fragment shader at preload:** both are documented in `PERFORMANCE_FINDINGS.md` but unfixed, because the fix needs a cache version bump.
