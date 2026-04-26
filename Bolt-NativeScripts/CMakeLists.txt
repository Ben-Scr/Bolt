cmake_minimum_required(VERSION 3.20)
project(Bolt-NativeScripts LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Dist" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DIST "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "Flags used by the C++ compiler for Dist builds." FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DIST "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "Flags used by the shared linker for Dist builds." FORCE)

function(bt_normalize_native_arch out_var raw_arch)
    string(TOLOWER "${raw_arch}" raw_arch_lower)
    if(raw_arch_lower STREQUAL "amd64" OR raw_arch_lower STREQUAL "x86_64" OR raw_arch_lower STREQUAL "x64")
        set(${out_var} "x86_64" PARENT_SCOPE)
    elseif(raw_arch_lower STREQUAL "arm64" OR raw_arch_lower STREQUAL "aarch64")
        set(${out_var} "arm64" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unsupported native script architecture: ${raw_arch}")
    endif()
endfunction()

set(BOLT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

file(GLOB_RECURSE NATIVE_SCRIPT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
)

add_library(Bolt-NativeScripts SHARED ${NATIVE_SCRIPT_SOURCES})

# Native scripts include the engine public headers for IntelliSense and template APIs.
# Non-inline runtime calls should still go through NativeEngineAPI or exported bindings.
target_include_directories(Bolt-NativeScripts PRIVATE
    "${BOLT_DIR}/Bolt-Engine/src"
    "${BOLT_DIR}/External/spdlog/include"
    "${BOLT_DIR}/External/glm"
    "${BOLT_DIR}/External/entt/src"
    "${BOLT_DIR}/External/stb"
    "${BOLT_DIR}/External/magic_enum/include"
    "${BOLT_DIR}/External/cereal/include"
    "${BOLT_DIR}/External/glfw/include"
    "${BOLT_DIR}/External/glad/include"
    "${BOLT_DIR}/External/miniaudio"
    "${BOLT_DIR}/External/box2d/include"
    "${BOLT_DIR}/External/Bolt-Physics/include"
    "${BOLT_DIR}/External/dotnet"
)

target_compile_definitions(Bolt-NativeScripts PRIVATE
    BOLT_ALL_MODULES=1
    $<$<CONFIG:Debug>:BT_DEBUG;_DEBUG>
    $<$<CONFIG:Release>:BT_RELEASE;NDEBUG>
    $<$<CONFIG:Dist>:BT_DIST;NDEBUG>
)

if(WIN32)
    bt_normalize_native_arch(BT_NATIVE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    set(BT_NATIVE_PLATFORM "windows-${BT_NATIVE_ARCH}")
    target_compile_definitions(Bolt-NativeScripts PRIVATE BT_PLATFORM_WINDOWS)
elseif(UNIX AND NOT APPLE)
    bt_normalize_native_arch(BT_NATIVE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    set(BT_NATIVE_PLATFORM "linux-${BT_NATIVE_ARCH}")
    target_compile_definitions(Bolt-NativeScripts PRIVATE BT_PLATFORM_LINUX)
else()
    message(FATAL_ERROR "Unsupported platform for native scripts")
endif()

target_link_directories(Bolt-NativeScripts PRIVATE
    "$<$<CONFIG:Debug>:${BOLT_DIR}/bin/Debug-${BT_NATIVE_PLATFORM}/Bolt-Engine>"
    "$<$<CONFIG:Release>:${BOLT_DIR}/bin/Release-${BT_NATIVE_PLATFORM}/Bolt-Engine>"
    "$<$<CONFIG:Dist>:${BOLT_DIR}/bin/Dist-${BT_NATIVE_PLATFORM}/Bolt-Engine>"
)
target_link_libraries(Bolt-NativeScripts PRIVATE Bolt-Engine)

if(MSVC)
    target_compile_options(Bolt-NativeScripts PRIVATE /utf-8)
endif()

set(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
foreach(config Debug Release Dist)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(Bolt-NativeScripts PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${ENGINE_ROOT}/bin/${config}-${BT_NATIVE_PLATFORM}/Bolt-NativeScripts"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${ENGINE_ROOT}/bin/${config}-${BT_NATIVE_PLATFORM}/Bolt-NativeScripts"
    )
endforeach()
