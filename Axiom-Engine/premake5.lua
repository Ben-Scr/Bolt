group "Core"
project "Axiom-Engine"
    location "."
    kind "SharedLib"
    language "C++"
    cppdialect "C++20"
    cdialect "C17"
    staticruntime "off"
    warnings "Extra"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    -- AIM_BUILD_DLL flips AXIOM_API to __declspec(dllexport) inside the engine TUs.
    -- Consumers (Editor / Launcher / Runtime / engine_core packages) define AIM_IMPORT_DLL.
    defines { "AIM_BUILD_DLL" }

    pchheader "pch.hpp"
    pchsource "src/pch.cpp"

    files
    {
        "src/**.h",
        "src/**.hpp",
        "src/**.c",
        "src/**.cpp"
    }

    local function RemoveFilesIfModuleDisabled(moduleEnabled, filePatterns)
        if not moduleEnabled then
            removefiles(filePatterns)
        end
    end

    local function RemoveFilesUnlessModulesEnabled(requiredModules, filePatterns)
        for _, moduleName in ipairs(requiredModules) do
            if not AxiomModules[moduleName] then
                removefiles(filePatterns)
                return
            end
        end
    end

    -- These integration units still aggregate multiple optional modules.
    RemoveFilesUnlessModulesEnabled({ "Render", "Audio", "Physics", "Scripting", "Editor" },
        {
            "src/Core/Application.*"
        }
    )

    RemoveFilesUnlessModulesEnabled({ "Render" },
        {
            "src/Core/Input.*",
            "src/Core/Window.*"
        }
    )

    RemoveFilesUnlessModulesEnabled({ "Physics" },
        {
            "src/Components/General/Transform2DComponent.cpp"
        }
    )

    RemoveFilesUnlessModulesEnabled({ "Render", "Audio", "Physics", "Scripting" },
        {
            "src/Scene/BuiltInComponentRegistration.cpp",
            "src/Scene/Entity.cpp",
            "src/Scene/EntityHelper.cpp",
            "src/Scene/Scene.cpp",
            "src/Scene/SceneDefinition.cpp",
            "src/Scene/SceneManager.cpp",
            "src/Serialization/SceneSerializer.cpp",
            "src/Serialization/SceneSerializerDeserialize.cpp"
        }
    )

    RemoveFilesIfModuleDisabled(AxiomModules.Render,
        {
            "src/Components/Graphics/**",
            "src/Graphics/**",
            "src/Gui/GuiRenderer.*",
            "src/Systems/GizmosDebugSystem.*",
            "src/Systems/ParticleUpdateSystem.*"
        }
    )

    RemoveFilesIfModuleDisabled(AxiomModules.Audio,
        {
            "src/Audio/**",
            "src/Components/Audio/**",
            "src/Systems/AudioUpdateSystem.*"
        }
    )

    RemoveFilesIfModuleDisabled(AxiomModules.Physics,
        {
            "src/Components/Physics/**",
            "src/Physics/**"
        }
    )

    RemoveFilesIfModuleDisabled(AxiomModules.Scripting,
        {
            "src/Scripting/**"
        }
    )

    -- Profiler is gated by the standalone --no-profiler premake flag, not
    -- by the module profile. When stripped, drop the entire Profiling/
    -- folder so no Tracy headers are pulled in and no symbols are emitted.
    RemoveFilesIfModuleDisabled(AxiomProfiler.Enabled,
        {
            "src/Profiling/**"
        }
    )

    -- Diagnostics/StatsOverlay.cpp uses ImGui directly, but the engine DLL
    -- doesn't link ImGui (it's an editor/runtime concern). The header
    -- (StatsOverlay.hpp) only forward-declares ImVec2 so it stays clean to
    -- include in engine code; the .cpp is excluded here and re-added by
    -- Axiom-Editor's and Axiom-Runtime's premake (same pattern as the
    -- ProfilerPanel cross-binary share).
    removefiles { "src/Diagnostics/**.cpp" }


    UseAxiomEngineModuleDependencies()
    defines(GetAxiomModuleDefines())

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
        "../External/Axiom-Physics/include"
    }

    local scriptingIncludes =
    {
        "../External/dotnet"
    }

    if AxiomModules.Render then
        AddPrivateIncludes(
            {
                "src/Core/Application.cpp",
                "src/Core/Application.hpp",
                "src/Core/Input.hpp",
                "src/Core/Window.cpp",
                "src/Core/Window.hpp",
                "src/Graphics/**",
                "src/Gui/**",
                "src/Systems/GizmosDebugSystem.cpp"
            },
            renderIncludes
        )
    end

    if AxiomModules.Audio then
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

    if AxiomModules.Physics then
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

    if AxiomModules.Scripting then
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

    filter "system:windows"
        -- /FS is required when MSBuild's parallel compilation (/MP) hands out the
        -- same vc143.pdb to multiple CL.EXE workers — without it, concurrent PDB
        -- writes raise C1041. /MP itself is enabled by passing "-m" to MSBuild
        -- from our automation paths; /FS makes that path safe.
        buildoptions { "/utf-8", "/FS" }
        systemversion "latest"
        defines { "AIM_PLATFORM_WINDOWS" }

    filter "system:linux"
        pic "On"
        defines { "AIM_PLATFORM_LINUX" }

    filter {}

    -- (Cereal include is provided globally via Dependency.EngineCore — the
    -- per-file scoping attempt didn't reliably propagate to vcxproj; see
    -- the commentary in Dependencies.lua next to %{IncludeDir.Cereal}.)

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

    -- BuiltInComponentRegistration.cpp pulls heavy template instantiations from every
    -- registered component's Properties::Make / inspector descriptors, blowing past
    -- the COFF section limit on MSVC without /bigobj.
    filter "files:**/BuiltInComponentRegistration.cpp"
        buildoptions { "/bigobj" }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        defines { "AIM_DEBUG", "_DEBUG", "AIM_TRACK_MEMORY" }

    filter "configurations:Release"
        runtime "Release"
        optimize "On"
        symbols "On"
        defines { "AIM_RELEASE", "NDEBUG" }

    filter "configurations:Dist"
        runtime "Release"
        optimize "Full"
        symbols "Off"
        defines { "AIM_DIST", "NDEBUG" }
