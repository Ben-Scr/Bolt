workspace "Axiom"
    architecture "x64"
    startproject "Axiom-Launcher"

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
    description = "Axiom module profile: full (default compatibility), core, or custom. Supplying any --with-* option without this flag selects custom.",
    allowed =
    {
        { "full", "Compatibility profile: render, audio, physics, scripting, editor, and AXIOM_ALL_MODULES." },
        { "core", "Core-only profile: no optional module defines or third-party module dependencies." },
        { "custom", "Enable only the optional modules requested with --with-render/--with-audio/--with-physics/--with-scripting/--with-editor." }
    }
}

newoption { trigger = "with-render", description = "Enable the Axiom render module dependencies and AXIOM_WITH_RENDER." }
newoption { trigger = "with-audio", description = "Enable the Axiom audio module dependencies and AXIOM_WITH_AUDIO." }
newoption { trigger = "with-physics", description = "Enable the Axiom physics module dependencies and AXIOM_WITH_PHYSICS." }
newoption { trigger = "with-scripting", description = "Enable the Axiom scripting module dependencies and AXIOM_WITH_SCRIPTING." }
newoption { trigger = "with-editor", description = "Enable the Axiom editor module dependencies and AXIOM_WITH_EDITOR. This also enables render dependencies." }

require("premake/fix_csharp_platforms")
include "Dependencies.lua"

local function HasExplicitModuleOption()
    return _OPTIONS["with-render"]
        or _OPTIONS["with-audio"]
        or _OPTIONS["with-physics"]
        or _OPTIONS["with-scripting"]
        or _OPTIONS["with-editor"]
end

local function ResolveAxiomModules()
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
        modules.Audio = true
        modules.Physics = true
        modules.Scripting = true
    end

    return modules
end

AxiomModules = ResolveAxiomModules()

Dependency.EngineSelectedModules = MergeDependencySets(
    Dependency.EngineCore,
    AxiomModules.Render and Dependency.EngineCoreRender or nil,
    AxiomModules.Audio and Dependency.EngineCoreAudio or nil,
    AxiomModules.Physics and Dependency.EngineCorePhysics or nil,
    AxiomModules.Scripting and Dependency.EngineCoreScripting or nil,
    AxiomModules.Editor and Dependency.EngineCoreEditor or nil
)

Dependency.EditorRuntimeCommon = MergeDependencySets(
    {
        DependsOn = { "Axiom-Engine" },
        Links = { "Axiom-Engine" }
    },
    Dependency.EngineSelectedModules
)

function GetAxiomModuleDefines()
    local hasApplication = AxiomModules.Render
        and AxiomModules.Audio
        and AxiomModules.Physics
        and AxiomModules.Scripting
        and AxiomModules.Editor

    local defines =
    {
        "AXIOM_WITH_RENDER=" .. (AxiomModules.Render and "1" or "0"),
        "AXIOM_WITH_AUDIO=" .. (AxiomModules.Audio and "1" or "0"),
        "AXIOM_WITH_PHYSICS=" .. (AxiomModules.Physics and "1" or "0"),
        "AXIOM_WITH_SCRIPTING=" .. (AxiomModules.Scripting and "1" or "0"),
        "AXIOM_WITH_EDITOR=" .. (AxiomModules.Editor and "1" or "0"),
        "AXIOM_WITH_APPLICATION=" .. (hasApplication and "1" or "0")
    }

    if AxiomModules.FullCompatibility then
        table.insert(defines, "AXIOM_ALL_MODULES=1")
    end

    return defines
end

function UseAxiomEngineModuleDependencies()
    UseDependencySet(Dependency.EngineSelectedModules)
end

-- Shared postbuild command: copy AxiomAssets into each target output directory.
CopyAxiomAssets = '{COPYDIR} "' .. path.join(ROOT_DIR, "Axiom-Runtime/AxiomAssets") .. '" "%{cfg.targetdir}/AxiomAssets"'

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
if AxiomModules.Editor then
    project "ImGui"
        location (path.join(ROOT_DIR, "premake/generated/ImGui"))
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

            -- Backend (Axiom uses GLFW + OpenGL3)
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

if AxiomModules.Render then
    include "premake/dependencies/glfw.lua"
    include "premake/dependencies/glad.lua"
end

if AxiomModules.Physics then
    include "premake/dependencies/box2d.lua"
    include "premake/dependencies/axiom_physics.lua"
end

include "Axiom-Engine"

if AxiomModules.Scripting then
    include "Axiom-ScriptCore"
    include "Axiom-Sandbox"
end

if AxiomModules.Editor then
    include "Axiom-Editor"
end

if AxiomModules.FullCompatibility then
    include "Axiom-Launcher"
    include "Axiom-Runtime"
end

group ""
