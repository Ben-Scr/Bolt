--
-- fix_csharp_platforms.lua
--
-- premake5 (beta5) does not emit <Platforms> or a default <Platform> for
-- SDK-style C# projects. This causes two problems:
--
--   1. MSBuild skips the project when the solution passes Platform=x64
--      (no <Platforms>x64</Platforms> declared)
--
--   2. `dotnet build -c Release` (without -p:Platform=x64) defaults to
--      AnyCPU, so the per-config OutputPath conditions never match and
--      the DLL lands in bin/Release/net9.0/ instead of the engine's
--      expected bin/Release-windows-x86_64/ path.
--
-- Fix: inject both <Platforms>x64</Platforms> and a default Platform
-- fallback into every C# .csproj after generation.

local function patchCsProj(csprojPath)
    local f = io.open(csprojPath, "r")
    if not f then return end
    local content = f:read("*a")
    f:close()

    local changed = false

    -- 1) Add <Platforms>x64</Platforms> if missing
    if not content:find("<Platforms>") and content:find("<Configurations>") then
        content = content:gsub(
            "(<Configurations>.-</Configurations>)",
            "%1\n    <Platforms>x64</Platforms>"
        )
        changed = true
    end

    -- 2) Default Platform to x64 so `dotnet build -c Release` (without
    --    -p:Platform=x64) matches the per-config OutputPath conditions.
    --    Must be unconditional because the .NET SDK sets AnyCPU before any
    --    conditional property in the project file is evaluated.
    --    Command-line -p:Platform=x64 still overrides this (MSBuild rules).
    if not content:find("<Platform>x64</Platform>") then
        content = content:gsub(
            "(<Platforms>x64</Platforms>)",
            "%1\n    <Platform>x64</Platform>"
        )
        changed = true
    end

    if changed then
        f = io.open(csprojPath, "w")
        f:write(content)
        f:close()
    end
end

premake.override(premake.action, "call", function(base, action)
    base(action)

    local wks = premake.global.eachWorkspace()()
    if wks then
        for prj in premake.workspace.eachproject(wks) do
            if prj.language == "C#" then
                patchCsProj(premake.filename(prj, ".csproj"))
            end
        end
    end
end)
