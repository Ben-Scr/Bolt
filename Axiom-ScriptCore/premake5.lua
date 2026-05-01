group "Core"
project "Axiom-ScriptCore"
    location "."
    kind "SharedLib"
    language "C#"
    dotnetframework "net9.0"
    clr "Unsafe"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    vsprops {
        AppendTargetFrameworkToOutputPath = "false",
        Nullable = "enable",
        AllowUnsafeBlocks = "true",
        EnableDynamicLoading = "true",
        TreatWarningsAsErrors = "true",
    }

    files {
        "Source/**.cs"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        optimize "Off"
        symbols "On"
        defines { "AIM_DEBUG" }

    filter "configurations:Release"
        optimize "On"
        symbols "On"
        defines { "AIM_RELEASE" }

    filter "configurations:Dist"
        optimize "Full"
        symbols "Off"
        defines { "AIM_DIST" }
