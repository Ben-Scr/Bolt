project "Glad"
    location (path.join(ROOT_DIR, "External/glad"))
    kind "StaticLib"
    language "C"
    cdialect "C17"
    staticruntime "off"

    targetdir (path.join(ROOT_DIR, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(ROOT_DIR, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files
    {
        path.join(ROOT_DIR, "External/glad/include/**.h"),
        path.join(ROOT_DIR, "External/glad/src/**.c")
    }

    includedirs
    {
        path.join(ROOT_DIR, "External/glad/include")
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
