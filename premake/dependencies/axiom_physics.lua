local AXIOM_PHYSICS_DIR = path.getabsolute(path.join(_SCRIPT_DIR, "../../External/Axiom-Physics"))
local AXIOM_PHYSICS_INCLUDE_DIR = path.join(AXIOM_PHYSICS_DIR, "include")
local AXIOM_PHYSICS_SOURCE_DIR = path.join(AXIOM_PHYSICS_DIR, "src")
local AXIOM_PHYSICS_GLM_INCLUDE_DIR = path.getabsolute(path.join(ROOT_DIR, IncludeDir["GLM"]))

project "Axiom-Physics"
    location (path.join(ROOT_DIR, "premake/generated/Axiom-Physics"))
    kind "StaticLib"
    language "C++"
    cppdialect "C++23"
    cdialect "C17"
    staticruntime "off"

    targetdir (path.join(ROOT_DIR, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(ROOT_DIR, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files
    {
        path.join(AXIOM_PHYSICS_INCLUDE_DIR, "**.h"),
        path.join(AXIOM_PHYSICS_INCLUDE_DIR, "**.hpp"),
        path.join(AXIOM_PHYSICS_SOURCE_DIR, "**.h"),
        path.join(AXIOM_PHYSICS_SOURCE_DIR, "**.hpp"),
        path.join(AXIOM_PHYSICS_SOURCE_DIR, "**.c"),
        path.join(AXIOM_PHYSICS_SOURCE_DIR, "**.cpp")
    }

 includedirs
    {
        AXIOM_PHYSICS_INCLUDE_DIR,
        AXIOM_PHYSICS_GLM_INCLUDE_DIR
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
