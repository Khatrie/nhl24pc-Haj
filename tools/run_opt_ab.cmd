@echo off
rem Experiments A+B: stream-buffer reads untracked, and T# image info cached (exact; see PERFORMANCE_ROADMAP.md).
set NHLPORT_OPT_STREAM_UNTRACKED=1
set NHLPORT_OPT_TEX_INFO_MEMO=1
start "" /D "%~dp0..\release" "%~dp0..\release\launcher.exe"
