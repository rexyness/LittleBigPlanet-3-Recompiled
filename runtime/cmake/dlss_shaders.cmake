# The DLSS upscaler's compute passes (runtime/src/gpu/dlss_shaders/dlss.comp, one SPIR-V per
# pass) -> embedded headers gpu/dlss_shaders/dlss_<pass>_comp.h, built like frame generation's
# (cmake/fg_shaders.cmake). A DLSS build only (LBP3_DLSS).
set(DLSS_SHADER_INCLUDE ${CMAKE_BINARY_DIR}/dlss_shaders/include)
set(DLSS_SHADER_DIR ${DLSS_SHADER_INCLUDE}/gpu/dlss_shaders)
set(DLSS_SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/dlss_shaders/dlss.comp)
set(DLSS_SHADER_HEADERS "")
foreach(PASS_ENTRY IN ITEMS mvdecode:1 compose:2 velscale:3)
    string(REPLACE ":" ";" PASS_PARTS ${PASS_ENTRY})
    list(GET PASS_PARTS 0 PASS_NAME)
    list(GET PASS_PARTS 1 PASS_NUM)
    string(TOUPPER "DLSS_${PASS_NAME}_COMP" PASS_SYMBOL)
    set(PASS_HEADER ${DLSS_SHADER_DIR}/dlss_${PASS_NAME}_comp.h)
    add_custom_command(
        OUTPUT ${PASS_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DLSS_SHADER_DIR}
        COMMAND ${FG_GLSLANG} --target-env vulkan1.3 -o ${PASS_HEADER} --vn ${PASS_SYMBOL} -DPASS=${PASS_NUM}
                ${DLSS_SHADER_SRC}
        COMMAND ${CMAKE_COMMAND} "-DHEADER_FILE=${PASS_HEADER}"
                -P ${SHADPS4}/src/video_core/host_shaders/AddShaderHeaderInclude.cmake
        DEPENDS ${FG_GLSLANG} ${DLSS_SHADER_SRC}
        COMMENT "Building DLSS shader pass ${PASS_NAME}"
        VERBATIM
    )
    list(APPEND DLSS_SHADER_HEADERS ${PASS_HEADER})
endforeach()
add_custom_target(dlss_shaders DEPENDS ${DLSS_SHADER_HEADERS} SOURCES ${DLSS_SHADER_SRC})
