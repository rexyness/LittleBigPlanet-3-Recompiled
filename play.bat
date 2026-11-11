@echo off
rem Starts LittleBigPlanet 3 (native). build.bat only compiles; this runs it.
rem Logs go to analysis\play_log.txt (errors, crashes) and analysis\play_stdout.txt.

rem --- options -------------------------------------------------------------
rem Skip the startup logos and live-action intro: 1 = skip, 0 = play them.
set SKIP_INTRO=1
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
