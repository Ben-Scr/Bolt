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

    UseDependencySet(Dependency.EngineCore)
    defines { "BT_TRACK_MEMORY" }

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
