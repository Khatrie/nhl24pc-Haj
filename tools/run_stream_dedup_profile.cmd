@echo off
rem Starts the launcher with draw-phase timing and the experimental stream buffer copy reuse.
rem Turn on performance logging in the launcher before pressing Play.
set NHLPORT_DIAG_DRAW_PHASES=1
set NHLPORT_STREAM_DEDUP=1
start "" /D "%~dp0..\release" "%~dp0..\release\launcher.exe"
