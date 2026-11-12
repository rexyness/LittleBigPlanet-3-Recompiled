@echo off
rem Starts LittleBigPlanet 3 (native). build.bat only compiles; this runs it.
rem Logs go to analysis\play_log.txt (errors, crashes) and analysis\play_stdout.txt.

rem --- options -------------------------------------------------------------
rem Skip the startup logos and live-action intro: 1 = skip, 0 = play them.
set SKIP_INTRO=1
rem Frame generation: a generated frame between each pair of real ones, so 60 on screen
rem while the game itself still runs at 30 (about +17 ms input lag): 1 = on, 0 = off.
set FRAMEGEN=1
rem Render resolution: 1440 = the game draws at 2560x1440 (patched; the PS4 version draws
rem 1920x1080), 1080 = the original 1920x1080, stretched to the window.
set RES=1440
rem LBP1/LBP2 items (the old "My Unlocks", whose servers are gone): 1 = run the game's own
rem import once, the first time the start menu opens; wait for "successfully imported",
rem don't press Circle, then close the game and set this back to 0 (a session that ran the
rem import crashed later when Options was pressed again). Back up your saves first.
set MY_UNLOCKS=0
rem ---------------------------------------------------------------------------

cd /d "%~dp0"
rem A scripted test run has the player's saves set aside and test saves in place (see the
rem planning repo's tools\saves.sh): playing now would play, and save over, the test saves.
if exist analysis\savedata_live_is_test (
    echo A test run has your saves set aside right now ^(analysis\savedata_live_is_test^).
    echo Wait until it has finished, then start the game again.
    pause
    exit /b 1
)
if not exist build\lbp3.exe (
    echo build\lbp3.exe not found - run build.bat first.
    pause
    exit /b 1
)
set EXTRA=
if "%SKIP_INTRO%"=="1" set EXTRA=--skip-intro
if "%FRAMEGEN%"=="1" (
    set LBP3_FRAMEGEN=1
    set LBP3_FRAMEGEN_MODE=warp
)
if "%RES%"=="1440" (
    set LBP3_RES=1440
    set LBP3_RES_SHADERS=1
)
if "%MY_UNLOCKS%"=="1" set LBP3_MY_UNLOCKS=lbp1,lbp2
echo Starting LittleBigPlanet 3... close the game window to quit.
if not exist analysis mkdir analysis
rem Every session's log is also kept for good in analysis\play_logs\, named by its last write.
powershell -NoProfile -ExecutionPolicy Bypass -File tools\archive_play_log.ps1
rem Keep the logs of the last three sessions (play_log.1.txt is the previous one).
if exist analysis\play_log.2.txt move /y analysis\play_log.2.txt analysis\play_log.3.txt >nul
if exist analysis\play_log.1.txt move /y analysis\play_log.1.txt analysis\play_log.2.txt >nul
if exist analysis\play_log.txt move /y analysis\play_log.txt analysis\play_log.1.txt >nul
build\lbp3.exe --run %EXTRA% > analysis\play_stdout.txt 2> analysis\play_log.txt
if errorlevel 1 (
    echo.
    echo The game stopped with an error. Details: analysis\play_log.txt
    pause
)
