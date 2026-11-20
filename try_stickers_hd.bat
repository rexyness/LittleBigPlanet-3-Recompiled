@echo off
rem PROTOTYPE (prototype/sticker-atlas): plays your game with this branch's build, to compare
rem stickers with the PS4's sticker atlas and with 512-px tiles (LBP3_STICKERS_HD=1).
rem   try_stickers_hd.bat          512-px sticker tiles
rem   try_stickers_hd.bat off      the PS4's atlas, same build (the "before")
rem Runs from your game folder (your saves), offline, 1440p with frame generation, intro skipped.
rem Logs go to analysis\stickers_play_log.txt; play.bat's logs are left alone.
set "GAME=D:\Recomps\lbp3-recompiled"
set "EXE=%~dp0build\lbp3.exe"
cd /d "%GAME%" || exit /b 1
if exist analysis\savedata_live_is_test (
    echo A test run has your saves set aside right now ^(analysis\savedata_live_is_test^).
    echo Wait until it has finished, then start again.
    pause
    exit /b 1
)
if not exist "%EXE%" (
    echo %EXE% not found - build this branch first ^(build.bat --target lbp3^).
    pause
    exit /b 1
)
set LBP3_RES=1440
set LBP3_RES_SHADERS=1
set LBP3_FRAMEGEN=1
set LBP3_FRAMEGEN_MODE=warp
set LBP3_WINDOW=borderless
set LBP3_STICKERS_HD=
if /i not "%~1"=="off" set LBP3_STICKERS_HD=1
if defined LBP3_STICKERS_HD (echo Sticker atlas: 512-px tiles) else (echo Sticker atlas: the PS4's)
echo Close the game window to quit.
if not exist analysis mkdir analysis
"%EXE%" --run --skip-intro > analysis\stickers_play_stdout.txt 2> analysis\stickers_play_log.txt
