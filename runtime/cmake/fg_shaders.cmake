# Frame generation's compute shaders (runtime/src/gpu/fg_shaders/fg_warp.comp, one SPIR-V per
# pass) -> embedded headers gpu/fg_shaders/fg_warp_<pass>_comp.h, built like shadPS4's host
# shaders: with the glslang compiled from shadPS4's externals, no outside tools.
if (TARGET glslang-standalone)
    set(FG_GLSLANG glslang-standalone)
elseif (TARGET glslang::glslang-standalone)
    set(FG_GLSLANG glslang::glslang-standalone)
endif()

set(FG_SHADER_INCLUDE ${CMAKE_BINARY_DIR}/fg_shaders/include)
set(FG_SHADER_DIR ${FG_SHADER_INCLUDE}/gpu/fg_shaders)
set(FG_SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/fg_shaders/fg_warp.comp)
set(FG_SHADER_HEADERS "")
foreach(PASS_ENTRY IN ITEMS hudshare:1 decide:2 luma0:3 down:4 gamevec:5 coarse:6 init:7 smooth:8 midinit:9 compose:10 textraw:11 textdil:12)
    string(REPLACE ":" ";" PASS_PARTS ${PASS_ENTRY})
    list(GET PASS_PARTS 0 PASS_NAME)
    list(GET PASS_PARTS 1 PASS_NUM)
    string(TOUPPER "FG_WARP_${PASS_NAME}_COMP" PASS_SYMBOL)
    set(PASS_HEADER ${FG_SHADER_DIR}/fg_warp_${PASS_NAME}_comp.h)
    add_custom_command(
        OUTPUT ${PASS_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${FG_SHADER_DIR}
        COMMAND ${FG_GLSLANG} --target-env vulkan1.3 -o ${PASS_HEADER} --vn ${PASS_SYMBOL} -DPASS=${PASS_NUM}
                ${FG_SHADER_SRC}
        COMMAND ${CMAKE_COMMAND} "-DHEADER_FILE=${PASS_HEADER}"
                -P ${SHADPS4}/src/video_core/host_shaders/AddShaderHeaderInclude.cmake
        DEPENDS ${FG_GLSLANG} ${FG_SHADER_SRC}
        COMMENT "Building frame-generation shader pass ${PASS_NAME}"
        VERBATIM
    )
    list(APPEND FG_SHADER_HEADERS ${PASS_HEADER})
endforeach()
add_custom_target(fg_shaders DEPENDS ${FG_SHADER_HEADERS} SOURCES ${FG_SHADER_SRC})
