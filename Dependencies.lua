IncludeDir = {}
IncludeDir["ExternalRoot"] = "External"
IncludeDir["ImGui"] = "External/imgui"
IncludeDir["Spdlog"] = "External/spdlog/include"
IncludeDir["GLFW"] = "External/glfw/include"
IncludeDir["AxiomPhysics"] = "External/Axiom-Physics/include"
IncludeDir["Box2D"] = "External/box2d/include"
IncludeDir["GLM"] = "External/glm"
IncludeDir["EnTT"] = "External/entt/src"
IncludeDir["STB"] = "External/stb"
IncludeDir["MagicEnum"] = "External/magic_enum/include"
IncludeDir["MiniAudio"] = "External/miniaudio"
IncludeDir["Cereal"] = "External/cereal/include"
IncludeDir["Glad"] = "External/glad/include"
IncludeDir["DotNet"] = "External/dotnet"
IncludeDir["AxiomEngine"] = "Axiom-Engine/src"
IncludeDir["AxiomEngineLegacy"] = "Axiom-Engine/src"

local isWindowsTarget = os.target() == "windows"

local function AppendUnique(target, values)
    if not values then
        return
    end

    local seen = {}
    for _, value in ipairs(target) do
        seen[value] = true
    end

    for _, value in ipairs(values) do
        if not seen[value] then
            table.insert(target, value)
            seen[value] = true
        end
    end
end

function MergeDependencySets(...)
    local merged =
    {
        IncludeDirs = {},
        LibDirs = {},
        DependsOn = {},
        Links = {}
    }

    for index = 1, select("#", ...) do
        local dependency = select(index, ...)
        if dependency then
            AppendUnique(merged.IncludeDirs, dependency.IncludeDirs)
            AppendUnique(merged.LibDirs, dependency.LibDirs)
            AppendUnique(merged.DependsOn, dependency.DependsOn)
            AppendUnique(merged.Links, dependency.Links)
        end
    end

    return merged
end

local MergeDependencies = MergeDependencySets

Library = {}
Library["GLFW"] = "glfw3.lib"
Library["Box2D"] = "box2d.lib"
Library["FreeType"] = "freetype.lib"
Library["OpenGL"] = "%{cfg.system == 'windows' and 'opengl32.lib' or 'GL'}"
Library["GDI32"] = "gdi32.lib"
Library["NetHost"] = isWindowsTarget and "nethost.lib" or "nethost"

LibDir = {}
if isWindowsTarget then
    LibDir["DotNet"] = "External/dotnet/lib"
end

Dependency = {}
Dependency["ImGui"] =
{
    IncludeDirs = { "%{IncludeDir.ImGui}", "%{IncludeDir.ImGui}/backends", "%{IncludeDir.GLFW}", "%{IncludeDir.Glad}" },
    BuildProject = true
}

Dependency["Spdlog"] =
{
    IncludeDirs = { "%{IncludeDir.Spdlog}" },
    HeaderOnly = true
}

Dependency["ExternalIncludes"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Box2D}",
        "%{IncludeDir.GLM}",
        "%{IncludeDir.EnTT}",
        "%{IncludeDir.STB}",
        "%{IncludeDir.MagicEnum}",
        "%{IncludeDir.MiniAudio}",
        "%{IncludeDir.Cereal}",
        "%{IncludeDir.Glad}",
        "%{IncludeDir.AxiomPhysics}"
    }
}

-- Minimal public core contract:
-- - Axiom-Engine/src headers
-- - Core/Export.hpp feature metadata
-- - Axiom.hpp lean umbrella
-- - header-only/public dependencies required by that core surface
Dependency["EngineCore"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.AxiomEngine}",
        "%{IncludeDir.AxiomEngineLegacy}",
        "%{IncludeDir.Spdlog}",
        "%{IncludeDir.GLM}",
        "%{IncludeDir.EnTT}",
        "%{IncludeDir.STB}",
        "%{IncludeDir.MagicEnum}",
        "%{IncludeDir.Cereal}"
    },

    LibDirs = {},

    DependsOn = {},

    Links = {}
}

Dependency["EngineCoreRender"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Glad}"
    },

    LibDirs = {},

    DependsOn =
    {
        "Glad",
        "GLFW"
    },

    Links =
    {
        "Glad",
        "GLFW",
        "%{Library.OpenGL}"
    }
}

Dependency["EngineCoreAudio"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.MiniAudio}"
    },

    LibDirs = {},

    DependsOn = {},

    Links = {}
}

Dependency["EngineCorePhysics"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.Box2D}",
        "%{IncludeDir.AxiomPhysics}"
    },

    LibDirs = {},

    DependsOn =
    {
        "Box2D",
        "Axiom-Physics"
    },

    Links =
    {
        "Box2D",
        "Axiom-Physics"
    }
}

Dependency["EngineCoreScripting"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.DotNet}"
    },

    LibDirs = {},

    DependsOn = {},

    Links =
    {
        "%{Library.NetHost}"
    }
}

Dependency["EngineCoreEditor"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.ImGui}/backends"
    },

    LibDirs = {},

    DependsOn =
    {
        "ImGui"
    },

    Links =
    {
        "ImGui"
    }
}

Dependency["EngineCoreAllModules"] = MergeDependencies(
    Dependency["EngineCore"],
    Dependency["EngineCoreRender"],
    Dependency["EngineCoreAudio"],
    Dependency["EngineCorePhysics"],
    Dependency["EngineCoreScripting"],
    Dependency["EngineCoreEditor"]
)

-- Explicit legacy/full-module compatibility path for consumers that opt into AXIOM_ALL_MODULES.
Dependency["EngineCoreLegacy"] = Dependency["EngineCoreAllModules"]

Dependency["EditorRuntimeCommon"] = MergeDependencies(
    {
        DependsOn = { "Axiom-Engine" },
        Links = { "Axiom-Engine" }
    },
    Dependency["EngineCoreAllModules"]
)

if LibDir["DotNet"] then
    table.insert(Dependency["EngineCoreScripting"].LibDirs, "%{LibDir.DotNet}")
    table.insert(Dependency["EngineCoreAllModules"].LibDirs, "%{LibDir.DotNet}")
    table.insert(Dependency["EditorRuntimeCommon"].LibDirs, "%{LibDir.DotNet}")
end
