@echo off
rem Builds nhl24.exe and launcher.exe and stages release\ (keeps user settings). Output: build\build_and_stage.log;
rem build\build_and_stage.done holds the exit code when finished.
cd /d "%~dp0.."
del /q build\build_and_stage.done 2>nul
echo [%date% %time%] build start > build\build_and_stage.log
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_nhlport.ps1 >> build\build_and_stage.log 2>&1
if errorlevel 1 (echo BUILD FAILED >> build\build_and_stage.log & echo 1 > build\build_and_stage.done & exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File launcher\build_launcher.ps1 >> build\build_and_stage.log 2>&1
if errorlevel 1 (echo LAUNCHER FAILED >> build\build_and_stage.log & echo 3 > build\build_and_stage.done & exit /b 3)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_release.ps1 >> build\build_and_stage.log 2>&1
if errorlevel 1 (echo RELEASE FAILED >> build\build_and_stage.log & echo 2 > build\build_and_stage.done & exit /b 2)
echo [%date% %time%] done >> build\build_and_stage.log
echo 0 > build\build_and_stage.done
