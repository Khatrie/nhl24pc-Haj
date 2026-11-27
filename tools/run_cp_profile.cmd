@echo off
rem Starts the launcher with the GPU command-thread sampling profiler (NHLPORT_CP_SAMPLE=1).
rem Samples go to release\user\log\nhl24_cp_samples-<session>.txt; analyse with
rem   python tools\cp_profile.py release\user\log
set NHLPORT_CP_SAMPLE=1
start "" /D "%~dp0..\release" "%~dp0..\release\launcher.exe"
