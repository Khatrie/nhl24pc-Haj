@echo off
rem Experiment: reuse stream-buffer copies of small read-only buffers within a submission
rem (NHLPORT_STREAM_REUSE=1, see PERFORMANCE_ROADMAP.md). Add "profile" to also time draw phases.
set NHLPORT_STREAM_REUSE=1
if /i "%~1"=="profile" set NHLPORT_DIAG_DRAW_PHASES=1
start "" /D "%~dp0..\release" "%~dp0..\release\launcher.exe"
