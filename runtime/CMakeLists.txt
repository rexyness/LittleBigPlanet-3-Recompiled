cmake_minimum_required(VERSION 3.25)
project(lbp3_runtime LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Build with clang-cl: game-facing functions need the System V ABI")
endif()

find_package(SDL3 REQUIRED CONFIG PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL3/cmake" NO_DEFAULT_PATH)

include(cmake/shader_recompiler.cmake)
include(cmake/video_core.cmake)

# Dear ImGui (MIT; shadPS4's copy) for the on-screen keyboard, drawn on the presenter's SDL
# renderer. Built with the stock imconfig.h: nothing else here compiles ImGui (video_core
# only sees stand-ins in runtime/shim/imgui).
set(IMGUI_DIR ${SHADPS4_EXT}/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_library(hle STATIC
    src/core/debugger.cpp
    src/core/linker.cpp
    src/core/module.cpp
    src/core/nid.cpp
    src/core/patches.cpp
    src/core/runner.cpp
    src/core/tls.cpp
    src/core/vfs.cpp
    src/hle/hle.cpp
    src/hle/app_content.cpp
    src/hle/my_unlocks.cpp
    src/hle/audioout.cpp
    src/hle/audio_downmix.cpp
    src/hle/trophy.cpp
    src/hle/fiber.cpp
    src/hle/dialogs.cpp
    src/hle/common_dialog.cpp
    src/hle/dialog_window.cpp
    src/hle/ime_dialog.cpp
    src/hle/jpeg.cpp
    src/hle/modal.cpp
    src/hle/gnm_packets.cpp
    src/hle/http_offline.cpp
    src/hle/kernel_fs.cpp
    src/hle/kernel_memory.cpp
    src/hle/kernel_module.cpp
    src/hle/kernel_pthread.cpp
    src/hle/kernel_sync.cpp
    src/hle/kernel_time.cpp
    src/hle/libkernel.cpp
    src/hle/libScePad.cpp
    src/hle/net_offline.cpp
    src/hle/np_offline.cpp
    src/hle/playgo.cpp
    src/hle/pm4.cpp
    src/hle/rtc.cpp
    src/hle/savedata.cpp
    src/hle/system_services.cpp
    src/hle/videoout.cpp
    src/input/gamepad_sdl.cpp
    src/input/actions.cpp
    src/input/game_state.cpp
    src/input/motion.cpp
    src/input/mouse_touch.cpp
    src/core/native_trace.cpp
    src/video/presenter.cpp
    src/video/framegen.cpp
    src/video/framegen_warp.cpp
    src/video/framegen_gpu.cpp
    src/ui/osk_model.cpp
    src/ui/overlay.cpp
    src/ui/osk_overlay.cpp
    src/ui/dialog_overlay.cpp
)
target_include_directories(hle PUBLIC src)
target_include_directories(hle PRIVATE ${SHADPS4_EXT}/stb)  # hle/jpeg.cpp
target_link_libraries(hle PUBLIC shader_recompiler video_core)
target_compile_definitions(hle PUBLIC _WIN32_WINNT=0x0A00 NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(hle PUBLIC imgui SDL3::SDL3 bcrypt winmm dbghelp onecore advapi32 dxgi comctl32)

add_executable(lbp3 src/main.cpp)
target_link_libraries(lbp3 PRIVATE hle)

add_executable(padtest tests/padtest.cpp)
target_link_libraries(padtest PRIVATE hle)

add_executable(jpegtest tests/jpegtest.cpp)
target_link_libraries(jpegtest PRIVATE hle)
target_include_directories(jpegtest PRIVATE ${SHADPS4_EXT}/stb)

add_executable(imetest tests/imetest.cpp)
target_link_libraries(imetest PRIVATE hle)

# Local co-op offline: user service events, pads per player, seating, remote input per player.
add_executable(cooptest tests/cooptest.cpp)
target_link_libraries(cooptest PRIVATE hle)

# System dialogs offline: drives MsgDialog/SaveDataDialog the way LBP3 does and answers the windows.
add_executable(dialogtest tests/dialogtest.cpp)
target_link_libraries(dialogtest PRIVATE hle)

# The on-screen keyboard offline: model checks, the overlay driven with a scripted pad, and
# screenshots of every page (osktest --shot <dir>); osktest --manual to try it by hand.
add_executable(osktest tests/osktest.cpp)
target_link_libraries(osktest PRIVATE hle)

# The system dialogs drawn over the game (ui/dialog_overlay): behaviour checks with a scripted
# pad, clock and keys, and screenshots (dlgtest --shot <dir>).
add_executable(dlgtest tests/dlgtest.cpp)
target_link_libraries(dlgtest PRIVATE hle)

# Frame generation offline (video/framegen, LBP3_FRAMEGEN=1): the generators, the drop rules,
# the presenter's due-time queue and the worker end to end.
add_executable(framegentest tests/framegentest.cpp)
target_link_libraries(framegentest PRIVATE hle)

# Frame generation offline evaluator (perf/fg_warp.md): runs the generators on dumped frames
# (analysis/fg_dump), scores the skip-one midpoint against the real frame, writes stills.
add_executable(fgeval tests/fgeval.cpp)
target_link_libraries(fgeval PRIVATE hle)
target_include_directories(fgeval PRIVATE ${SHADPS4_EXT}/stb)

# Audio offline (ticket 16): the 7.1 -> stereo downmix and limiter against expected levels,
# SDL's own matrix measured through a real SDL stream, and libSceAudioOut on SDL's dummy device.
add_executable(audiotest tests/audiotest.cpp)
target_link_libraries(audiotest PRIVATE hle)

# Add-on content offline (ticket 18): param.sfo parsing, the pack scan, and the list/key calls
# the way LBP3 makes them (addconttest [real add-on root] also summarises a real folder).
add_executable(addconttest tests/addconttest.cpp)
target_link_libraries(addconttest PRIVATE hle)

# The shared shader cache for the 1440p rescale (perf/shared_cache.md): which shaders get a
# salted cache identity (shadPS4 common/lbp3_res.h).
add_executable(rescachetest tests/rescachetest.cpp)
target_link_libraries(rescachetest PRIVATE hle)

# LBP3_MY_UNLOCKS's import guard (ticket 19): the import's cancel button is held off while it runs.
add_executable(unlocktest tests/unlocktest.cpp)
target_link_libraries(unlocktest PRIVATE hle)

# Motion and the mouse as the touch pad offline (input/motion, input/mouse_touch): the gyro offset
# learning, the orientation filter's drift, the virtual tilt, and the mouse's touch gestures.
add_executable(motiontest tests/motiontest.cpp)
target_link_libraries(motiontest PRIVATE hle)

foreach(exe lbp3 padtest jpegtest imetest dialogtest osktest dlgtest cooptest framegentest fgeval audiotest addconttest rescachetest unlocktest motiontest)
    add_custom_command(TARGET ${exe} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:${exe}>)
endforeach()
