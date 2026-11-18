cmake_minimum_required(VERSION 3.21)

# Runtime library must be decided before any target exists.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:DEBUG>:Debug>DLL")

project(
    Nivalis
    VERSION 0.5.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

# /utf-8: spdlog's bundled fmt refuses to compile without it.
# /EHsc:  CommonLibSSE does not set it and MSVC warns C4530 otherwise.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8 /EHsc /permissive-")

# Release keeps full optimisation but also emits a .pdb.  Without it the dll
# carries no CodeView (RSDS) record at all, so no .pdb could ever match it and
# every WER minidump / CrashLogger帧 would stay a bare module+RVA.  /FS is
# required because /Zi + parallel compilation writes one shared compiler .pdb.
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi /FS")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG:FULL")

# IMPORTANT: spdlog/fmt MUST be linked statically into the plugin. A DLL that
# imports spdlog.dll/fmt.dll cannot be loaded by SKSE (Error 126: module not
# found) on a clean install, and the SMF menu never registers. With vcpkg use
# -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DVCPKG_HOST_TRIPLET=... .
#
# Header-only spdlog assembled under D:/dev/deps (no vcpkg needed).
# On CI / another machine pass -DCMAKE_PREFIX_PATH=<spdlog prefix> instead.
if(EXISTS "D:/dev/deps/lib/cmake/spdlog")
    list(APPEND CMAKE_PREFIX_PATH "D:/dev/deps")
endif()

# Keep the dependency root overridable so the same tree builds on a machine
# (or CI runner) that does not have D:/dev/CommonLibSSE-NG.
set(COMMONLIBSSE_NG_ROOT "D:/dev/CommonLibSSE-NG" CACHE PATH
    "CommonLibSSE-NG source root")
if(NOT EXISTS "${COMMONLIBSSE_NG_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "CommonLibSSE-NG not found at ${COMMONLIBSSE_NG_ROOT}. "
        "Pass -DCOMMONLIBSSE_NG_ROOT=<path>.")
endif()

set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

# One DLL per runtime. CommonLibSSE-NG's REL::Module::GetRuntime() becomes a
# compile-time constant when only one of ENABLE_SKYRIM_SE / ENABLE_SKYRIM_AE
# is defined: an AE-only build ALWAYS looks for versionlib-*.bin even on a
# 1.5.97 executable -> "failed to open address library file". SE and AE must
# be separate binaries. Select with -DNIVALIS_TARGET_RUNTIME=SE|AE.
set(NIVALIS_TARGET_RUNTIME "AE" CACHE STRING "Target runtime: AE or SE")
set_property(CACHE NIVALIS_TARGET_RUNTIME PROPERTY STRINGS AE SE)

if(NIVALIS_TARGET_RUNTIME STREQUAL "SE")
    set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
    set(ENABLE_SKYRIM_AE OFF CACHE BOOL "" FORCE)
    set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
elseif(NIVALIS_TARGET_RUNTIME STREQUAL "AE")
    set(ENABLE_SKYRIM_SE OFF CACHE BOOL "" FORCE)
    set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
    set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
    # CommonLibSSE-NG's CMake never defines this, but several headers (TES.h
    # worldSpace offset, BGSSaveLoadManager, ControlMap, ...) gate their AE
    # layout on it. Without it TES::worldSpace compiles at the SE offset 0x140
    # while 1.6.x runtimes keep it at 0x148. Must be set BEFORE
    # add_subdirectory so the static lib and this plugin agree.
    add_compile_definitions(SKYRIM_SUPPORT_AE=1)
else()
    message(FATAL_ERROR "NIVALIS_TARGET_RUNTIME must be AE or SE")
endif()

message(STATUS "Nivalis target runtime: ${NIVALIS_TARGET_RUNTIME}")

# rapidcsv 是 CommonLibSSE-NG 的硬依赖（src/REL/ID.cpp 解析地址库 CSV）。
# vcpkg 的 rapidcsv 端口 cmake_minimum_required < 3.5，在 CI 的 CMake 4 上
# 直接配置失败，所以改为把单头文件随仓库提供；NG 的 CMakeLists 正是通过
# RAPIDCSV_INCLUDE_DIRS 找它，这里预置好让它的 find_path 直接采用。
set(RAPIDCSV_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/external/rapidcsv" CACHE PATH
    "Directory containing rapidcsv.h")

add_subdirectory("${COMMONLIBSSE_NG_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/CommonLibSSE-NG")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/mmsf")

# The SKSE Menu Framework API header includes <windows.h>, which collides
# with CommonLibSSE-NG's parallel REX::W32 types under the force-included
# PCH. MenuMMSF.cpp therefore compiles on its own and talks to the plugin
# only through the plain-data MenuBridge.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/MenuMMSF.cpp"
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)

target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.h")

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${PROJECT_NAME}"
    PREFIX ""
)
