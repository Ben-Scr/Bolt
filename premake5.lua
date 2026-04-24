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

newoption
{
    trigger = "module-profile",
    value = "PROFILE",
    description = "Bolt module profile: full (default compatibility), core, or custom. Supplying any --with-* option without this flag selects custom.",
    allowed =
    {
        { "full", "Compatibility profile: render, audio, physics, scripting, editor, and BOLT_ALL_MODULES." },
        { "core", "Core-only profile: no optional module defines or third-party module dependencies." },
        { "custom", "Enable only the optional modules requested with --with-render/--with-audio/--with-physics/--with-scripting/--with-editor." }
    }
}

newoption { trigger = "with-render", description = "Enable the Bolt render module dependencies and BOLT_WITH_RENDER." }
newoption { trigger = "with-audio", description = "Enable the Bolt audio module dependencies and BOLT_WITH_AUDIO." }
newoption { trigger = "with-physics", description = "Enable the Bolt physics module dependencies and BOLT_WITH_PHYSICS." }
newoption { trigger = "with-scripting", description = "Enable the Bolt scripting module dependencies and BOLT_WITH_SCRIPTING." }
newoption { trigger = "with-editor", description = "Enable the Bolt editor module dependencies and BOLT_WITH_EDITOR. This also enables render dependencies." }

require("premake/fix_csharp_platforms")
include "Dependencies.lua"

local function HasExplicitModuleOption()
    return _OPTIONS["with-render"]
        or _OPTIONS["with-audio"]
        or _OPTIONS["with-physics"]
        or _OPTIONS["with-scripting"]
        or _OPTIONS["with-editor"]
end

local function ResolveBoltModules()
    local profile = _OPTIONS["module-profile"]
    if not profile then
        profile = HasExplicitModuleOption() and "custom" or "full"
    end

    local modules =
    {
        Profile = profile,
        FullCompatibility = profile == "full",
        Render = false,
        Audio = false,
        Physics = false,
        Scripting = false,
        Editor = false
    }

    if profile == "full" then
        modules.Render = true
        modules.Audio = true
        modules.Physics = true
        modules.Scripting = true
        modules.Editor = true
    elseif profile == "custom" then
        modules.Render = _OPTIONS["with-render"] or false
        modules.Audio = _OPTIONS["with-audio"] or false
        modules.Physics = _OPTIONS["with-physics"] or false
        modules.Scripting = _OPTIONS["with-scripting"] or false
        modules.Editor = _OPTIONS["with-editor"] or false
    end

    if modules.Editor then
        modules.Render = true
    end

    return modules
end

BoltModules = ResolveBoltModules()

Dependency.EngineSelectedModules = MergeDependencySets(
    Dependency.EngineCore,
    BoltModules.Render and Dependency.EngineCoreRender or nil,
    BoltModules.Audio and Dependency.EngineCoreAudio or nil,
    BoltModules.Physics and Dependency.EngineCorePhysics or nil,
    BoltModules.Scripting and Dependency.EngineCoreScripting or nil,
    BoltModules.Editor and Dependency.EngineCoreEditor or nil
)

Dependency.EditorRuntimeCommon = MergeDependencySets(
    {
        DependsOn = { "Bolt-Engine" },
        Links = { "Bolt-Engine" }
    },
    Dependency.EngineSelectedModules
)

function GetBoltModuleDefines()
    local defines =
    {
        "BT_TRACK_MEMORY",
        "BOLT_WITH_RENDER=" .. (BoltModules.Render and "1" or "0"),
        "BOLT_WITH_AUDIO=" .. (BoltModules.Audio and "1" or "0"),
        "BOLT_WITH_PHYSICS=" .. (BoltModules.Physics and "1" or "0"),
        "BOLT_WITH_SCRIPTING=" .. (BoltModules.Scripting and "1" or "0"),
        "BOLT_WITH_EDITOR=" .. (BoltModules.Editor and "1" or "0")
    }

    if BoltModules.FullCompatibility then
        table.insert(defines, "BOLT_ALL_MODULES=1")
    end

    return defines
end

function UseBoltEngineModuleDependencies()
    UseDependencySet(Dependency.EngineSelectedModules)
end

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
if BoltModules.Editor then
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
end

if BoltModules.Render then
    include "premake/dependencies/glfw.lua"
    include "premake/dependencies/glad.lua"
end

if BoltModules.Physics then
    include "premake/dependencies/box2d.lua"
    include "premake/dependencies/bolt_physics.lua"
end

include "Bolt-Engine"

if BoltModules.Scripting then
    include "Bolt-ScriptCore"
    include "Bolt-Sandbox"
end

if BoltModules.Editor then
    include "Bolt-Editor"
end

if BoltModules.FullCompatibility then
    include "Bolt-Launcher"
    include "Bolt-Runtime"
end

group ""
