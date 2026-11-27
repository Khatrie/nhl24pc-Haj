@echo off
rem Experiment A: stream-buffer reads are not barrier-tracked (exact; see PERFORMANCE_ROADMAP.md).
set NHLPORT_OPT_STREAM_UNTRACKED=1
start "" /D "%~dp0..\release" "%~dp0..\release\launcher.exe"
