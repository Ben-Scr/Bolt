workspace "Bolt"
    architecture "x64"
    startproject "Bolt-Launcher"

    configurations
    {
        "Debug",
        "Release",
        "Dist"
    }

    filter { "system:windows", "language:C++" }
        toolset "v143"

    filter {}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
ROOT_DIR = os.realpath(_MAIN_SCRIPT_DIR)

newoption
{
    trigger = "with-imgui-demo",
    description = "Include imgui_demo.cpp in the ImGui static library project."
}

require("premake/fix_csharp_platforms")
include "Dependencies.lua"

-- Shared postbuild command: copy BoltAssets into each target output directory.
CopyBoltAssets = '{COPYDIR} "' .. path.join(ROOT_DIR, "Bolt-Runtime/BoltAssets") .. '" "%{cfg.targetdir}/BoltAssets"'

local function NormalizeRootPath(pathValue)
    if path.isabsolute(pathValue) then
        return pathValue
    end

    return path.join(ROOT_DIR, pathValue)
end

local function NormalizeRootPaths(paths)
    local normalized = {}

    for _, pathValue in ipairs(paths) do
        table.insert(normalized, NormalizeRootPath(pathValue))
    end

    return normalized
end

function UseDependencySet(dep)
    if dep.IncludeDirs then
        includedirs(NormalizeRootPaths(dep.IncludeDirs))
    end

    if dep.LibDirs then
        libdirs(NormalizeRootPaths(dep.LibDirs))
    end

    if dep.DependsOn then
        dependson(dep.DependsOn)
    end

    if dep.Links then
        links(dep.Links)
    end
end

group "Dependencies"
project "ImGui"
    location "External/imgui"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

targetdir (path.join(ROOT_DIR, "bin/" .. outputdir .. "/%{prj.name}"))
objdir (path.join(ROOT_DIR, "bin-int/" .. outputdir .. "/%{prj.name}"))

files
    {
        -- Core
        "External/imgui/imconfig.h",
        "External/imgui/imgui.h",
        "External/imgui/imgui_internal.h",
        "External/imgui/imstb_rectpack.h",
        "External/imgui/imstb_textedit.h",
        "External/imgui/imstb_truetype.h",
        "External/imgui/imgui.cpp",
        "External/imgui/imgui_draw.cpp",
        "External/imgui/imgui_tables.cpp",
        "External/imgui/imgui_widgets.cpp",

        -- Backend (Bolt uses GLFW + OpenGL3)
        "External/imgui/backends/imgui_impl_glfw.h",
        "External/imgui/backends/imgui_impl_glfw.cpp",
        "External/imgui/backends/imgui_impl_opengl3.h",
        "External/imgui/backends/imgui_impl_opengl3.cpp",
        "External/imgui/backends/imgui_impl_opengl3_loader.h"
    }

    if _OPTIONS["with-imgui-demo"] then
        files { "External/imgui/imgui_demo.cpp" }
    end

    vpaths
    {
        ["Core/*"] =
        {
            "External/imgui/imconfig.h",
            "External/imgui/imgui.h",
            "External/imgui/imgui_internal.h",
            "External/imgui/imstb_rectpack.h",
            "External/imgui/imstb_textedit.h",
            "External/imgui/imstb_truetype.h",
            "External/imgui/imgui.cpp",
            "External/imgui/imgui_draw.cpp",
            "External/imgui/imgui_tables.cpp",
            "External/imgui/imgui_widgets.cpp"
        },
        ["Backends/*"] =
        {
            "External/imgui/backends/imgui_impl_glfw.h",
            "External/imgui/backends/imgui_impl_glfw.cpp",
            "External/imgui/backends/imgui_impl_opengl3.h",
            "External/imgui/backends/imgui_impl_opengl3.cpp",
            "External/imgui/backends/imgui_impl_opengl3_loader.h"
        },
        ["Optional/*"] = { "External/imgui/imgui_demo.cpp" }
    }

    UseDependencySet(Dependency.ImGui)

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        defines { "_DEBUG" }

    filter "configurations:Release"
        runtime "Release"
        optimize "On"
        symbols "On"
        defines { "NDEBUG" }

    filter "configurations:Dist"
        runtime "Release"
        optimize "Full"
        symbols "Off"
        defines { "NDEBUG" }

    filter {}

include "premake/dependencies/glfw.lua"
include "premake/dependencies/box2d.lua"
include "premake/dependencies/glad.lua"
include "premake/dependencies/bolt_physics.lua"

include "Bolt-Engine"
include "Bolt-ScriptCore"
include "Bolt-Sandbox"
include "Bolt-Editor"
include "Bolt-Launcher"
include "Bolt-Runtime"

group ""
