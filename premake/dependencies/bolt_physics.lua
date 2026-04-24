local BOLT_PHYSICS_DIR = path.getabsolute(path.join(_SCRIPT_DIR, "../../External/Bolt-Physics"))
local BOLT_PHYSICS_INCLUDE_DIR = path.join(BOLT_PHYSICS_DIR, "include")
local BOLT_PHYSICS_SOURCE_DIR = path.join(BOLT_PHYSICS_DIR, "src")
local BOLT_PHYSICS_GLM_INCLUDE_DIR = path.getabsolute(path.join(ROOT_DIR, IncludeDir["GLM"]))

project "Bolt-Physics"
    location (BOLT_PHYSICS_DIR)
    kind "StaticLib"
    language "C++"
    cppdialect "C++23"
    cdialect "C17"
    staticruntime "off"

    targetdir (path.join(ROOT_DIR, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(ROOT_DIR, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files
    {
        path.join(BOLT_PHYSICS_INCLUDE_DIR, "**.h"),
        path.join(BOLT_PHYSICS_INCLUDE_DIR, "**.hpp"),
        path.join(BOLT_PHYSICS_SOURCE_DIR, "**.h"),
        path.join(BOLT_PHYSICS_SOURCE_DIR, "**.hpp"),
        path.join(BOLT_PHYSICS_SOURCE_DIR, "**.c"),
        path.join(BOLT_PHYSICS_SOURCE_DIR, "**.cpp")
    }

 includedirs
    {
        BOLT_PHYSICS_INCLUDE_DIR,
        BOLT_PHYSICS_GLM_INCLUDE_DIR
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
