# NHL 24 PC

Play your own PS4 copy of NHL 24 on Windows. This fork of HajDEO’s NHL 24 port uses shadPS4 to implement PS4 system services and translate graphics through Vulkan. The game’s x86-64 code runs directly on the host CPU.

## Install and play

- **Requirements:** Windows 10 or 11 (x64), a Vulkan 1.3 GPU with current drivers, 8 GB of RAM (16 GB recommended), and roughly 40 GB of free space for the extracted game. DLSS requires a supported NVIDIA RTX GPU and the NVIDIA NGX runtime.
- **Supported base packages:** `CUSA37933` (US) and `CUSA37934` (EU), version `01.00`. Start with the base game. The bundled patch file also has entries for `01.60`, but compatibility with the update has limited verification, especially across regions. [Bundled patches](game_patches/nhl24.xml)

1. Extract the Windows package to a writable folder and keep its files together. Use the build steps below for the features in this checkout. The parent fork also hosts its own released builds. [Parent fork releases](https://github.com/hilals/nhl24pc/releases)
2. Open `launcher.exe`, select **Install PKG**, and choose your own NHL 24 base-game package. The launcher displays the detected title ID and version and extracts the game into `game\` beside the launcher.
3. Select **Play**. Xbox and PlayStation controllers are detected automatically. Use **Advanced → Controls** to change keyboard mappings.
4. Back up `user\` before replacing or moving a build. It holds local saves, settings, trophy data, and caches. Keep your original title ID when moving an existing career or profile.

## Launcher and graphics

**Simple** shows the common options. **Advanced** contains **Controls**, **Display**, **Diagnostics**, and **Performance**.

Set **Local profile name** on the **Simple** screen and choose **Save** or **Play**. The new name takes effect the next time the game starts. It keeps the same local profile ID, saves, and trophies. If you sign in to ShadNet, the game can display your ShadNet online ID instead.

| Setting | What it does |
| --- | --- |
| **Graphics presets** | **Compatibility**, **Balanced**, and **Quality** set several graphics options together. **Apply preset** previews the changes in the launcher, **Restore previous** undoes the preview, and **Save** or **Play** keeps it. |
| **Display and scaling** | Choose windowed or fullscreen output, resolutions up to 4K, ultrawide stretch, FSR with optional RCAS sharpening, or DLSS Super Resolution and DLAA on supported RTX hardware. Output resolution describes the window or display image. |
| **Frame rate and menus** | The launcher offers 60, 90, 120, and 144 FPS caps. Gameplay is typically near 60 FPS in the observed build, while native menus and cutscenes often run near 30 FPS. **Smooth menus & cutscenes** is opt-in and allows presentation on each display refresh, subject to the cap and frames the game supplies. A higher cap does not guarantee more unique frames. |
| **Image quality** | Experimental **PS4 Pro mode**, 16× anisotropic filtering, and texture crispness controls are under **Display**. Use **Compatibility** as a baseline if you encounter rendering issues. |
| **GPU readbacks** | **Relaxed** is the default and the recommended starting point for missing or black ice. It can also be selected in the in-game **Graphics** tab. |
| **Pipeline cache** | **Performance** reports the Vulkan driver cache separately from the emulator’s shader and pipeline files. A driver cache of only a few bytes can coexist with a populated shader cache. Its size depends on the driver and the pipeline data available when it is saved. |

The build includes fixes for texture-cache synchronization, image resizing and copies, and inherited shader fixes for crashes in **Create a Team** and **Be a Pro**. It also reduces redundant Vulkan pipeline binds and descriptor update overhead and reuses Windows waitable timers. First runs and new scenes can still hitch as shaders compile and assets upload. Performance gains from the latest changes have yet to be benchmarked.

The fork also supports an optional Utah Mammoth jersey and roster mod when the corresponding files are available. [Utah texture notes](docs/UTAH_CREST_NOTES.md)

In **Advanced → Controls**, choose PlayStation, Xbox, or Nintendo button prompts before launching the game. The Xbox and Nintendo packs replace supported button textures in menus and gameplay. Some controller illustrations and split prompt variants still use the game's original artwork. Button inputs follow the configured controller mapping.

## In-game settings and trophies

- **Open settings:** Press **Menu + View** together on an Xbox-style controller, or **Options + Share/Create** on a PlayStation-style controller. The translucent overlay starts near the upper left and can be moved or resized. Use the D-pad or left stick to navigate and the shoulder buttons to switch tabs.
- **Apply & save:** Use **Graphics** to toggle **Show FPS**, FSR, stretch, and smoother menus during play. Settings marked **restart**, such as resolution, present mode, HDR, DLSS, readbacks, and the frame cap, take effect after relaunching. Choose **Later** to keep playing or **Restart now** after saving any game progress.
- **Trophies:** The tab shows local earned and locked status, grade icons, and numeric goals where identifiable. Partial progress is available only when the game exposes the relevant count. NHL awards supported trophies through its normal unlock calls, and earned trophies can show a notification in the upper right. Trophy names and descriptions require successfully extracted metadata from your game. Keep local key material private.

## Troubleshooting and diagnostics

- **Wrong controller actions:** If a face button opens the wrong screen, check for Steam Input or another remapper. Close Steam or disable Steam Input for the launcher shortcut, then relaunch and test the controller.
- **Black ice or flicker:** Start with **Compatibility** and **Relaxed** GPU readbacks, then restart. Intermittent **Be a Pro** conversation flicker has been observed. A short clip and captures of the affected scene help identify the cause.
- **Match-load crash:** If the game exits with `0xc0000005` after team selection, try **Red zone protection (Windows)** in the launcher and relaunch.
- **Measure or capture a problem:** Open **Advanced → Diagnostics → Play with diagnostics**. Press **F1** when the issue occurs. Keep **F1 marks timing only** enabled for performance measurements. Clear it to add screenshots, draw logs, and GPU image dumps for graphics issues. Use **Open logs** and **Open captures** to find the session output.
- **Investigate stutter or high CPU use:** `user/log/nhl24_frames.csv` records presented-frame timing, `nhl24_threads.csv` records per-thread CPU usage, and `nhl24_diag.log` records events and summaries. Comparing the same sequence after the caches have warmed helps separate compilation and loading hitches from sustained frame-pacing issues.
- **Missing runtime DLL:** If Windows reports a missing `VCRUNTIME140.dll` or `MSVCP140.dll`, install the Microsoft Visual C++ 2015–2022 x64 Redistributable.

Share logs and relevant footage after checking for private paths. Keep game files, PKGs, saves, keys, and complete shader caches out of issue attachments.

## Build from source

Initialize the Git submodules. On Windows, the build uses Visual Studio C++ Build Tools and a Windows SDK, LLVM (`clang-cl` and `lld`), CMake, Ninja, and Python. The WinForms launcher uses the .NET Framework C# compiler.

```powershell
git submodule update --init --recursive
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_nhlport.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File launcher\build_launcher.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_release.ps1 -Out build\release-stage
```

The emulator and launcher build into `build\`. Stage into a separate directory when updating an existing installation to preserve its `user\` configuration. The release script assembles the executables, launcher images and user template. Optional mod and DLSS assets depend on the distribution.

See [PROJECT_STATUS.md](PROJECT_STATUS.md) for implementation notes, known issues, verification status, and pending work. Diagnostic developers can use [the run script](tools/run_nhl24.ps1), [the measurement script](tools/measure_nhl24.ps1), and [the capture report tool](tools/capture_report.py).

## Credits and code provenance

- **HajDEO** created the original NHL 24 PC port, including the base launcher and NHL-specific integration. **hilals** maintains the parent fork from which this checkout inherits its DLSS work, display and PS4 Pro options, pipeline-cache persistence, and other fixes. [Original NHL 24 PC port](https://github.com/HajDEO/nhl24pc) · [Parent fork](https://github.com/hilals/nhl24pc)
- **The shadPS4 team and contributors** built the underlying emulator. Recent renderer backports adapt **johulk**’s subresource-preservation work, **BeneausVillach**’s stale image-copy fix, **DanielSvoboda**’s subresource extent checks, and **Gih-pt**’s redundant pipeline-bind reduction. [shadPS4](https://github.com/shadps4-emu/shadPS4) · [PR #5433](https://github.com/shadps4-emu/shadPS4/pull/5433) · [PR #5444](https://github.com/shadps4-emu/shadPS4/pull/5444) · [PR #5524](https://github.com/shadps4-emu/shadPS4/pull/5524) · [PR #5726](https://github.com/shadps4-emu/shadPS4/pull/5726)
- **LEVASTAK’s NHL24PC V3.2** supplied the dynamic-resolution patch signatures, the approach for synchronizing an image that overlaps a texel buffer, and the sampler changes for forced anisotropic filtering and mip bias. See commits `bf5d6b8b`, `c9ce4671`, and `1fb3bc6d`.
- **NVIDIA** provides DLSS and the NGX runtime. DLSS is a trademark of NVIDIA. See [third-party license notices](LICENSES/) for additional dependencies.
- **Xelu / Those Awesome Guys** created the controller prompt artwork, released under CC0. The prompt textures are adapted from the [Xelu controller prompts](https://github.com/DJLink/Xelu_Free_Controller-Key_Prompts).

Support the parent fork and the original port through their respective pages. [hilals](https://ko-fi.com/hishak) · [HajDEO](https://ko-fi.com/E1E61VGHII)

## License and game data

The project is distributed under [GPL-2.0-or-later](LICENSE). Use your own legally dumped PS4 game package. The project is unaffiliated with EA, the NHL, and Sony.
