@echo off
set "VS=C:\Program Files\Microsoft Visual Studio\18\Community"
set "PATH=%VS%\VC\Tools\Llvm\x64\bin;%PATH%"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set SDL=D:\Recomps\lbp3-recompiled\third_party\SDL3
clang-cl /nologo /O2 /EHsc /std:c++20 /I"%SDL%\include" vsync_probe.cpp /link /LIBPATH:"%SDL%\lib\x64" SDL3.lib dxgi.lib dwmapi.lib winmm.lib psapi.lib shlwapi.lib /out:vsync_probe.exe || exit /b 1
copy /y "%SDL%\lib\x64\SDL3.dll" . >nul
