@echo off
rem Starts the launcher with per-draw phase timing in nhl24_frames.csv (see PERFORMANCE_ROADMAP.md).
rem Turn on performance logging in the launcher before pressing Play.
set NHLPORT_DIAG_DRAW_PHASES=1
start "" /D "%~dp0..\release" "%~dp0..\release\launcher.exe"
