@echo off
rem The dlss worktree's DLSS build: build.bat's toolchain, with LBP3_DLSS on and the SDK outside the repo.
cd /d D:\Recomps\lbp3-wt\dlss || exit /b 1
set "VS=C:\Program Files\Microsoft Visual Studio\18\Community"
set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%VS%\VC\Tools\Llvm\x64\bin;%PATH%"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cmake -S runtime -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_C_COMPILER=clang-cl -DLBP3_DLSS=ON -DLBP3_DLSS_SDK=D:/Recomps/sdk/dlss-310.9.16 || exit /b 1
cmake --build build --target lbp3 %*
