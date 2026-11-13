# shadNet client (the PSN stand-in LBPPC's community server runs: sign-in, friends, presence; later
# invites and the peer connection), opt-in at run time with LBP3_SHADNET=1 (net/shadnet_service.h).
#
# The client is shadPS4's (rpcs3/rpcsn lineage), GPL-2.0-or-later, as vendored by LBPPC
# (emurphy04, https://github.com/emurphy04/LBPPC at aad6a91, gpu/bbnet/vendor/shadps4/shadnet) with
# their TLS proxy support; copied into src/net/shadnet with our "lbp3:" changes (see NOTICE.md).
# Its dependencies come from shadPS4's externals: abseil + protobuf (and protoc, for the generated
# message code) and LibreSSL (TLS).
# Requires shader_recompiler.cmake (SHADPS4, SHADPS4_EXT, fmt).

# --- abseil + protobuf ------------------------------------------------------------
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(NOT TARGET absl::strings)
    add_subdirectory(${SHADPS4_EXT}/abseil-cpp ${CMAKE_BINARY_DIR}/ext/abseil-cpp EXCLUDE_FROM_ALL)
endif()
set(protobuf_BUILD_TESTS              OFF CACHE INTERNAL "")
set(protobuf_BUILD_EXAMPLES           OFF CACHE INTERNAL "")
set(protobuf_BUILD_PROTOC_BINARIES    ON  CACHE INTERNAL "")
set(protobuf_INSTALL                  OFF CACHE INTERNAL "")
set(protobuf_WITH_ZLIB                OFF CACHE INTERNAL "")
set(protobuf_BUILD_SHARED_LIBS        OFF CACHE INTERNAL "")
set(protobuf_LOCAL_DEPENDENCIES_ONLY  ON  CACHE INTERNAL "")
set(protobuf_FORCE_FETCH_DEPENDENCIES OFF CACHE INTERNAL "")
set(protobuf_MSVC_STATIC_RUNTIME      OFF CACHE BOOL "" FORCE)
add_subdirectory(${SHADPS4_EXT}/protobuf ${CMAKE_BINARY_DIR}/ext/protobuf EXCLUDE_FROM_ALL)

# --- LibreSSL (TLS to a server behind a TLS proxy, such as play.lbppc.com's shadNet) -----------
set(LIBRESSL_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(LIBRESSL_APPS OFF CACHE BOOL "" FORCE)
set(LIBRESSL_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${SHADPS4_EXT}/libressl ${CMAKE_BINARY_DIR}/ext/libressl EXCLUDE_FROM_ALL)

# --- the generated message code ---------------------------------------------------------------
set(SHADNET_PROTO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/net/shadnet/shadnet.proto)
set(SHADNET_PROTO_OUT ${CMAKE_BINARY_DIR}/shadnet_proto_gen)
file(MAKE_DIRECTORY ${SHADNET_PROTO_OUT})
add_custom_command(
    OUTPUT ${SHADNET_PROTO_OUT}/shadnet.pb.cc ${SHADNET_PROTO_OUT}/shadnet.pb.h
    COMMAND protobuf::protoc --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/src/net/shadnet
            --cpp_out=${SHADNET_PROTO_OUT} ${SHADNET_PROTO_SRC}
    DEPENDS ${SHADNET_PROTO_SRC} protobuf::protoc
    COMMENT "Generating the shadNet protobuf messages"
    VERBATIM)

# --- the client ---------------------------------------------------------------------------------
add_library(shadnet_client STATIC
    src/net/shadnet/client.cpp
    src/net/shadnet/server_probe.cpp
    ${SHADNET_PROTO_OUT}/shadnet.pb.cc)
set_target_properties(shadnet_client PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
# No runtime/shim here: its absl stand-ins would shadow the real abseil protobuf needs.
target_include_directories(shadnet_client
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/net        # "shadnet/client.h"
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/net/shadnet ${SHADNET_PROTO_OUT} ${SHADPS4}/src
            ${SHADPS4_EXT}/libressl/include)
target_compile_definitions(shadnet_client PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
target_compile_options(shadnet_client PRIVATE -Wno-everything)
target_link_libraries(shadnet_client PUBLIC fmt::fmt PRIVATE libprotobuf ssl crypto ws2_32 crypt32)
