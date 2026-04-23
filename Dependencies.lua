IncludeDir = {}
IncludeDir["ExternalRoot"] = "External"
IncludeDir["ImGui"] = "External/imgui"
IncludeDir["Spdlog"] = "External/spdlog/include"
IncludeDir["GLFW"] = "External/glfw/include"
IncludeDir["BoltPhysics"] = "External/Bolt-Physics/include"
IncludeDir["Box2D"] = "External/box2d/include"
IncludeDir["GLM"] = "External/glm"
IncludeDir["EnTT"] = "External/entt/src"
IncludeDir["STB"] = "External/stb"
IncludeDir["MagicEnum"] = "External/magic_enum/include"
IncludeDir["MiniAudio"] = "External/miniaudio"
IncludeDir["Cereal"] = "External/cereal/include"
IncludeDir["Glad"] = "External/glad/include"
IncludeDir["DotNet"] = "External/dotnet"
IncludeDir["BoltEngine"] = "Bolt-Engine/src"
IncludeDir["BoltEngineLegacy"] = "Bolt-Engine/src"

local isWindowsTarget = os.target() == "windows"

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
        "%{IncludeDir.BoltPhysics}"
    }
}

Dependency["EngineCore"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.BoltEngine}",
        "%{IncludeDir.BoltEngineLegacy}",
        "%{IncludeDir.Spdlog}",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Glad}",
        "%{IncludeDir.Box2D}",
        "%{IncludeDir.GLM}",
        "%{IncludeDir.EnTT}",
        "%{IncludeDir.STB}",
        "%{IncludeDir.MagicEnum}",
        "%{IncludeDir.MiniAudio}",
        "%{IncludeDir.Cereal}",
        "%{IncludeDir.BoltPhysics}",
        "%{IncludeDir.DotNet}"
    },

    LibDirs = {},

    DependsOn =
    {
        "Glad",
        "GLFW",
        "Box2D",
        "Bolt-Physics"
    },

    Links =
    {
        "Glad",
        "GLFW",
        "Box2D",
        "Bolt-Physics",
        "%{Library.OpenGL}",
        "%{Library.NetHost}"
    }
}

Dependency["EditorRuntimeCommon"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.BoltEngine}",
        "%{IncludeDir.BoltEngineLegacy}",
        "%{IncludeDir.Spdlog}",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Glad}",
        "%{IncludeDir.Box2D}",
        "%{IncludeDir.GLM}",
        "%{IncludeDir.EnTT}",
        "%{IncludeDir.STB}",
        "%{IncludeDir.MagicEnum}",
        "%{IncludeDir.MiniAudio}",
        "%{IncludeDir.Cereal}",
        "%{IncludeDir.BoltPhysics}",
        "%{IncludeDir.DotNet}"
    },

    LibDirs = {},

    DependsOn =
    {
        "Bolt-Engine",
        "ImGui",
        "Glad",
        "GLFW",
        "Box2D",
        "Bolt-Physics"
    },

    Links =
    {
        "Bolt-Engine",
        "ImGui",
        "Glad",
        "GLFW",
        "%{Library.OpenGL}",
        "Box2D",
        "Bolt-Physics",
        "%{Library.NetHost}"
    }
}

if LibDir["DotNet"] then
    table.insert(Dependency["EngineCore"].LibDirs, "%{LibDir.DotNet}")
    table.insert(Dependency["EditorRuntimeCommon"].LibDirs, "%{LibDir.DotNet}")
end
