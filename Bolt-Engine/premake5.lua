group "Core"
project "Bolt-Engine"
    location "."
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    cdialect "C17"
    staticruntime "off"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    pchheader "pch.hpp"
    pchsource "src/pch.cpp"

    files
    {
        "src/**.h",
        "src/**.hpp",
        "src/**.c",
        "src/**.cpp"
    }

    UseBoltEngineModuleDependencies()
    defines(GetBoltModuleDefines())

    local function AddPrivateIncludes(filePatterns, includePaths)
        for _, pattern in ipairs(filePatterns) do
            filter("files:" .. pattern)
                includedirs(includePaths)
        end

        filter {}
    end

    local renderIncludes =
    {
        "../External/glfw/include",
        "../External/glad/include"
    }

    local audioIncludes =
    {
        "../External/miniaudio"
    }

    local physicsIncludes =
    {
        "../External/box2d/include",
        "../External/Bolt-Physics/include"
    }

    local scriptingIncludes =
    {
        "../External/dotnet"
    }

    local editorIncludes =
    {
        "../External/imgui",
        "../External/imgui/backends"
    }

    if BoltModules.Render then
        AddPrivateIncludes(
            {
                "src/Core/Application.cpp",
                "src/Core/Application.hpp",
                "src/Core/Input.hpp",
                "src/Core/Window.cpp",
                "src/Core/Window.hpp",
                "src/Graphics/**",
                "src/Gui/**",
                "src/Systems/GizmosDebugSystem.cpp",
                "src/Systems/ImGuiDebugSystem.cpp",
                "src/Systems/LauncherLayer.cpp"
            },
            renderIncludes
        )
    end

    if BoltModules.Audio then
        AddPrivateIncludes(
            {
                "src/Audio/**",
                "src/Components/Audio/**",
                "src/Scripting/ScriptBindings.cpp",
                "src/Scripting/ScriptBindingsScene.cpp",
                "src/Serialization/SceneSerializer.cpp",
                "src/Serialization/SceneSerializerDeserialize.cpp",
                "src/Serialization/SceneSerializerShared.hpp"
            },
            audioIncludes
        )
    end

    if BoltModules.Physics then
        AddPrivateIncludes(
            {
                "src/Core/Application.cpp",
                "src/Core/Application.hpp",
                "src/Components/Physics/**",
                "src/Physics/**",
                "src/Scene/Scene.cpp",
                "src/Scripting/ScriptBindings.cpp",
                "src/Scripting/ScriptBindingsScene.cpp",
                "src/Serialization/SceneSerializer.cpp",
                "src/Serialization/SceneSerializerDeserialize.cpp"
            },
            physicsIncludes
        )
    end

    if BoltModules.Scripting then
        AddPrivateIncludes(
            {
                "src/Scene/BuiltInComponentRegistration.cpp",
                "src/Scene/Scene.cpp",
                "src/Scene/SceneManager.cpp",
                "src/Scripting/**",
                "src/Serialization/SceneSerializer.cpp",
                "src/Serialization/SceneSerializerDeserialize.cpp"
            },
            scriptingIncludes
        )
    end

    if BoltModules.Editor then
        AddPrivateIncludes(
            {
                "src/Core/Application.cpp",
                "src/Core/Application.hpp",
                "src/Gui/**",
                "src/Scripting/ScriptSystem.cpp",
                "src/Systems/ImGuiDebugSystem.cpp",
                "src/Systems/LauncherLayer.cpp"
            },
            editorIncludes
        )
    end

    filter "system:windows"
        buildoptions { "/utf-8" }
        systemversion "latest"
        defines { "BT_PLATFORM_WINDOWS" }

    filter "system:linux"
        pic "On"
        defines { "BT_PLATFORM_LINUX" }

    filter {}

    filter "files:**/glad.c"
        flags { "NoPCH" }

    filter "files:**/ImGuiEditorSystem.cpp"
        flags { "NoPCH" }

    filter "files:**/ScriptEngine.cpp"
        flags { "NoPCH" }

    filter "files:**/ScriptBindings.cpp"
        flags { "NoPCH" }

    filter "files:**/DotNetHost.cpp"
        flags { "NoPCH" }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        defines { "BT_DEBUG", "_DEBUG" }

    filter "configurations:Release"
        runtime "Release"
        optimize "On"
        symbols "On"
        defines { "BT_RELEASE", "NDEBUG" }

    filter "configurations:Dist"
        runtime "Release"
        optimize "Full"
        symbols "Off"
        defines { "BT_DIST", "NDEBUG" }
