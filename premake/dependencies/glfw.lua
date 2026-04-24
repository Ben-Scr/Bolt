project "GLFW"
    location (path.join(ROOT_DIR, "External/glfw"))
    kind "StaticLib"
    language "C"
    staticruntime "off"

    targetdir (path.join(ROOT_DIR, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(ROOT_DIR, "bin-int/" .. outputdir .. "/%{prj.name}"))

    local function glfwFile(relpath)
        return path.join(ROOT_DIR, "External/glfw", relpath)
    end

    files
    {
        glfwFile("include/GLFW/glfw3.h"),
        glfwFile("include/GLFW/glfw3native.h"),
        glfwFile("src/internal.h"),
        glfwFile("src/platform.h"),
        glfwFile("src/mappings.h"),
        glfwFile("src/context.c"),
        glfwFile("src/init.c"),
        glfwFile("src/input.c"),
        glfwFile("src/monitor.c"),
        glfwFile("src/platform.c"),
        glfwFile("src/vulkan.c"),
        glfwFile("src/window.c"),
        glfwFile("src/egl_context.c"),
        glfwFile("src/osmesa_context.c")
    }

    includedirs
    {
        glfwFile("include"),
        glfwFile("src")
    }

    filter "system:windows"
        systemversion "latest"
        defines { "_GLFW_WIN32", "_CRT_SECURE_NO_WARNINGS" }
        files
        {
            glfwFile("src/win32_platform.h"),
            glfwFile("src/win32_joystick.h"),
            glfwFile("src/wgl_context.h"),
            glfwFile("src/win32_init.c"),
            glfwFile("src/win32_joystick.c"),
            glfwFile("src/win32_monitor.c"),
            glfwFile("src/win32_thread.c"),
            glfwFile("src/win32_time.c"),
            glfwFile("src/win32_window.c"),
            glfwFile("src/wgl_context.c")
        }

    filter "system:linux"
        pic "On"
        defines { "_GLFW_X11" }
        files
        {
            glfwFile("src/x11_platform.h"),
            glfwFile("src/xkb_unicode.h"),
            glfwFile("src/glx_context.h"),
            glfwFile("src/linux_joystick.h"),
            glfwFile("src/posix_poll.h"),
            glfwFile("src/posix_time.h"),
            glfwFile("src/posix_thread.h"),
            glfwFile("src/x11_init.c"),
            glfwFile("src/x11_monitor.c"),
            glfwFile("src/x11_window.c"),
            glfwFile("src/xkb_unicode.c"),
            glfwFile("src/glx_context.c"),
            glfwFile("src/linux_joystick.c"),
            glfwFile("src/posix_poll.c"),
            glfwFile("src/posix_time.c"),
            glfwFile("src/posix_thread.c")
        }
        links { "X11", "Xrandr", "Xi", "Xxf86vm", "Xcursor", "Xinerama", "Xext", "Xrender", "pthread", "dl", "m" }

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

    filter {}

    local optionalSources =
    {
        "src/win32_module.c",
        "src/null_init.c",
        "src/null_monitor.c",
        "src/null_window.c",
        "src/null_joystick.c"
    }

    for _, relpath in ipairs(optionalSources) do
        local fullpath = glfwFile(relpath)
        if os.isfile(fullpath) then
            files { fullpath }
        end
    end
