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
include(cmake/shadnet.cmake)  # opt-in at run time (LBP3_SHADNET=1)

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
    src/core/res_patches.cpp
    src/core/sync_probe.cpp
    src/core/runner.cpp
    src/core/tls.cpp
    src/core/vfs.cpp
    src/hle/hle.cpp
    src/hle/app_content.cpp
    src/hle/my_unlocks.cpp
    src/hle/file_edits.cpp
    src/hle/quit.cpp
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
    src/hle/http_online.cpp
    src/hle/online.cpp
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
    src/hle/np_shadnet.cpp
    src/net/shadnet_service.cpp
    src/net/web_request.cpp
    src/net/p2p_link.cpp
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
    src/video/live_stats.cpp
    src/ui/osk_model.cpp
    src/ui/overlay.cpp
    src/ui/boot_overlay.cpp
    src/ui/osk_overlay.cpp
    src/ui/dialog_overlay.cpp
    src/ui/menu_model.cpp
    src/ui/menu_overlay.cpp
)
target_include_directories(hle PUBLIC src)
target_include_directories(hle PRIVATE ${SHADPS4_EXT}/stb)  # hle/jpeg.cpp
target_link_libraries(hle PUBLIC shader_recompiler video_core shadnet_client)
target_compile_definitions(hle PUBLIC _WIN32_WINNT=0x0A00 NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(hle PUBLIC imgui SDL3::SDL3 bcrypt winmm dbghelp onecore advapi32 dxgi comctl32 winhttp ws2_32 iphlpapi)

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

# Online play offline checks: login ticket layout, reply signing, off without a server.
add_executable(onlinetest tests/onlinetest.cpp)
target_link_libraries(onlinetest PRIVATE hle)

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

add_executable(restest tests/restest.cpp)
target_link_libraries(restest PRIVATE hle)

# LBP3_MY_UNLOCKS's import guard (ticket 19): the import's cancel button is held off while it runs.
add_executable(unlocktest tests/unlocktest.cpp)
target_link_libraries(unlocktest PRIVATE hle)

# The driver pipeline cache file (fix/pipeline-warmup): which saved caches are handed to the driver.
add_executable(vkcachetest tests/vkcachetest.cpp)
target_link_libraries(vkcachetest PRIVATE hle)

# The best-of-both-worlds UX (best/README.md): the virtual file edits behind Quit Game in the
# pause menu, the pause menu's two-press logic, the in-game menu's model, the language codes.
add_executable(uxtest tests/uxtest.cpp)
target_link_libraries(uxtest PRIVATE hle)

# Motion and the mouse as the touch pad offline (input/motion, input/mouse_touch): the gyro offset
# learning, the orientation filter's drift, the virtual tilt, and the mouse's touch gestures.
add_executable(motiontest tests/motiontest.cpp)
target_link_libraries(motiontest PRIVATE hle)

# LBP3Launcher.exe: the settings window (launcher/). Our own code; it needs only ImGui, SDL3 and
# Windows (not the runtime), so it builds next to lbp3.exe without linking the game.
# miniz (externals, MIT): zlib for the Quit Game writer and zip for the release updater.
add_library(launcher_miniz STATIC
    ${SHADPS4_EXT}/miniz/miniz.c
    ${SHADPS4_EXT}/miniz/miniz_tdef.c
    ${SHADPS4_EXT}/miniz/miniz_tinfl.c
    ${SHADPS4_EXT}/miniz/miniz_zip.c)
target_include_directories(launcher_miniz PUBLIC ${SHADPS4_EXT}/miniz launcher)
target_compile_definitions(launcher_miniz PUBLIC MINIZ_NO_TIME _CRT_SECURE_NO_WARNINGS)
target_compile_options(launcher_miniz PRIVATE -Wno-everything)

add_library(launcher_core STATIC
    launcher/launcher_settings.cpp
    launcher/launcher_sys.cpp
    launcher/launcher_account.cpp
    launcher/launcher_accounts.cpp
    launcher/launcher_refresh.cpp
    launcher/launcher_friends.cpp
    launcher/launcher_quitmenu.cpp
    launcher/launcher_release.cpp
    launcher/launcher_input.cpp
    launcher/launcher_dump.cpp
    launcher/launcher_proc.cpp
    launcher/launcher_update.cpp
    launcher/launcher_discord.cpp
    launcher/launcher_stats.cpp
    launcher/launcher_shell.cpp
)
target_include_directories(launcher_core PUBLIC launcher)
target_compile_definitions(launcher_core PUBLIC _CRT_SECURE_NO_WARNINGS)
target_include_directories(launcher_core PRIVATE ${SHADPS4_EXT}/stb)  # the dump's PNGs (launcher_shell)
# nlohmann/json (externals, MIT) for the web APIs; input/action_table.h (shared with the runtime;
# SDL's gamepad enum only, nothing linked from SDL).
target_include_directories(launcher_core PUBLIC ${SHADPS4_EXT}/json/include src
    $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(launcher_core PUBLIC winhttp bcrypt shell32 advapi32 ole32 uuid propsys launcher_miniz)
# LBP3_PORT_VERSION: a packaged build's version (tools/make_package.ps1); empty in dev builds, which
# read SOURCE.md beside the launcher instead.
set(LBP3_PORT_VERSION "" CACHE STRING "The package version compiled into the launcher (v0.1.0-lbp3port)")
if(LBP3_PORT_VERSION)
    target_compile_definitions(launcher_core PRIVATE LBP3_PORT_VERSION="${LBP3_PORT_VERSION}")
endif()
# shadNet for the launcher (Friends, Test sign-in, Create account): the runtime's own client.
add_library(launcher_net STATIC launcher/launcher_net.cpp launcher/launcher_net_glue.cpp)
set_target_properties(launcher_net PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(launcher_net PRIVATE ${SHADPS4}/src ${SHADPS4_EXT}/libressl/include)
target_compile_definitions(launcher_net PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_link_libraries(launcher_net PUBLIC launcher_core shadnet_client PRIVATE ssl crypto fmt::fmt ws2_32)
# The window, with its own icon and version information (launcher.rc), and the port's motion
# filter (input/motion, standalone) for the controller tester.
add_executable(LBP3Launcher WIN32 launcher/launcher_main.cpp launcher/launcher_theme.cpp launcher/launcher_pages.cpp
    launcher/launcher_online.cpp launcher/launcher.rc src/input/motion.cpp)
target_include_directories(LBP3Launcher PRIVATE src)
target_link_libraries(LBP3Launcher PRIVATE launcher_core launcher_net imgui SDL3::SDL3)
# Its offline test: the ini, the settings -> LBP3_* mapping, names and addresses, JSON, SHA-512,
# the zip, saves backup/restore on a temporary folder, param.sfo and archive checks, git log
# parsing, the log readers, Discord's frames against a fake pipe, and the process runner.
add_executable(launchertest tests/launchertest.cpp)
target_link_libraries(launchertest PRIVATE launcher_core launcher_net libprotobuf)
target_include_directories(launchertest PRIVATE ${SHADNET_PROTO_OUT})  # the fake shadNet server's replies

# shadNet offline (net/shadnet_service, best/shadnet_design.md): the settings, off means no client
# (no thread, no socket), and a login against an in-process mock server speaking the protocol.
add_executable(shadnettest tests/shadnettest.cpp)
target_link_libraries(shadnettest PRIVATE hle libprotobuf)
target_include_directories(shadnettest PRIVATE ${SHADNET_PROTO_OUT})

# Online co-op's peer connections (net/p2p_link, hle/net_remote.h): two players in-process with a
# mock of shadNet's UDP service (STUN, relay), join protection, and the bridge to the game's sockets.
add_executable(p2ptest tests/p2ptest.cpp)
target_link_libraries(p2ptest PRIVATE hle)

# Every game-visible clock advances 1 s per second (no test needs the game).
add_executable(clocktest tests/clocktest.cpp)
target_link_libraries(clocktest PRIVATE hle)

foreach(exe lbp3 padtest jpegtest imetest dialogtest osktest dlgtest cooptest framegentest fgeval audiotest addconttest rescachetest restest unlocktest vkcachetest uxtest motiontest shadnettest p2ptest clocktest LBP3Launcher)
    add_custom_command(TARGET ${exe} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:${exe}>)
endforeach()
