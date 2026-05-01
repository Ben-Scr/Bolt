cmake_minimum_required(VERSION 3.20)
project(Axiom-NativeScripts LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Dist" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DIST "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "Flags used by the C++ compiler for Dist builds." FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DIST "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "Flags used by the shared linker for Dist builds." FORCE)

function(aim_normalize_native_arch out_var raw_arch)
    string(TOLOWER "${raw_arch}" raw_arch_lower)
    if(raw_arch_lower STREQUAL "amd64" OR raw_arch_lower STREQUAL "x86_64" OR raw_arch_lower STREQUAL "x64")
        set(${out_var} "x86_64" PARENT_SCOPE)
    elseif(raw_arch_lower STREQUAL "arm64" OR raw_arch_lower STREQUAL "aarch64")
        set(${out_var} "arm64" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unsupported native script architecture: ${raw_arch}")
    endif()
endfunction()

set(AXIOM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

file(GLOB_RECURSE NATIVE_SCRIPT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
)

add_library(Axiom-NativeScripts SHARED ${NATIVE_SCRIPT_SOURCES})

# Native scripts include the engine public headers for IntelliSense and template APIs.
# Non-inline runtime calls should still go through NativeEngineAPI or exported bindings.
target_include_directories(Axiom-NativeScripts PRIVATE
    "${AXIOM_DIR}/Axiom-Engine/src"
    "${AXIOM_DIR}/External/spdlog/include"
    "${AXIOM_DIR}/External/glm"
    "${AXIOM_DIR}/External/entt/src"
    "${AXIOM_DIR}/External/stb"
    "${AXIOM_DIR}/External/magic_enum/include"
    "${AXIOM_DIR}/External/cereal/include"
    "${AXIOM_DIR}/External/glfw/include"
    "${AXIOM_DIR}/External/glad/include"
    "${AXIOM_DIR}/External/miniaudio"
    "${AXIOM_DIR}/External/box2d/include"
    "${AXIOM_DIR}/External/Axiom-Physics/include"
    "${AXIOM_DIR}/External/dotnet"
)

target_compile_definitions(Axiom-NativeScripts PRIVATE
    AXIOM_ALL_MODULES=1
    $<$<CONFIG:Debug>:AIM_DEBUG;_DEBUG>
    $<$<CONFIG:Release>:AIM_RELEASE;NDEBUG>
    $<$<CONFIG:Dist>:AIM_DIST;NDEBUG>
)

if(WIN32)
    aim_normalize_native_arch(AIM_NATIVE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    set(AIM_NATIVE_PLATFORM "windows-${AIM_NATIVE_ARCH}")
    target_compile_definitions(Axiom-NativeScripts PRIVATE AIM_PLATFORM_WINDOWS)
elseif(UNIX AND NOT APPLE)
    aim_normalize_native_arch(AIM_NATIVE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    set(AIM_NATIVE_PLATFORM "linux-${AIM_NATIVE_ARCH}")
    target_compile_definitions(Axiom-NativeScripts PRIVATE AIM_PLATFORM_LINUX)
else()
    message(FATAL_ERROR "Unsupported platform for native scripts")
endif()

target_link_directories(Axiom-NativeScripts PRIVATE
    "$<$<CONFIG:Debug>:${AXIOM_DIR}/bin/Debug-${AIM_NATIVE_PLATFORM}/Axiom-Engine>"
    "$<$<CONFIG:Release>:${AXIOM_DIR}/bin/Release-${AIM_NATIVE_PLATFORM}/Axiom-Engine>"
    "$<$<CONFIG:Dist>:${AXIOM_DIR}/bin/Dist-${AIM_NATIVE_PLATFORM}/Axiom-Engine>"
)
target_link_libraries(Axiom-NativeScripts PRIVATE Axiom-Engine)

if(MSVC)
    target_compile_options(Axiom-NativeScripts PRIVATE /utf-8)
endif()

set(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
foreach(config Debug Release Dist)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(Axiom-NativeScripts PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${ENGINE_ROOT}/bin/${config}-${AIM_NATIVE_PLATFORM}/Axiom-NativeScripts"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${ENGINE_ROOT}/bin/${config}-${AIM_NATIVE_PLATFORM}/Axiom-NativeScripts"
    )
endforeach()
