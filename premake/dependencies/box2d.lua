project "Box2D"
    location (path.join(ROOT_DIR, "premake/generated/Box2D"))
    kind "StaticLib"
    language "C++"
    cppdialect "C++23"
    cdialect "C17"
    staticruntime "off"

    targetdir (path.join(ROOT_DIR, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(ROOT_DIR, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files
    {
        path.join(ROOT_DIR, "External/box2d/include/**.h"),
        path.join(ROOT_DIR, "External/box2d/src/**.h"),
        path.join(ROOT_DIR, "External/box2d/src/**.c"),
        path.join(ROOT_DIR, "External/box2d/src/**.cpp")
    }

    includedirs
    {
        path.join(ROOT_DIR, "External/box2d/include"),
        path.join(ROOT_DIR, "External/box2d/src")
    }

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
