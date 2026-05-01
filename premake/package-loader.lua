-- Axiom package system — Phase A loader.
--
-- Discovers `axiom-package.lua` manifests under `packages/<Name>/`, validates them,
-- topologically sorts by dependency, and registers Premake projects:
--
--   - engine_core  / standalone_cpp  →  Pkg.<Name>.Native  (C++ SharedLib)
--   - csharp                          →  Pkg.<Name>          (C# SharedLib)
--
-- A single package may declare any combination of the three layers. When both
-- engine_core and standalone_cpp are present they are merged into a single
-- Pkg.<Name>.Native (and that lib then links the engine, since engine_core does).
--
-- Manifest schema (returned by axiom-package.lua via `return { ... }`):
--
--   name          string   required, unique
--   version       string   required
--   description   string   optional
--   layers        table    required, at least one of engine_core / standalone_cpp / csharp
--                          each layer:
--                              sources           list of file/glob patterns relative to the package dir
--                              includes          optional list of include dirs (relative)
--                              private_includes  optional list of include dirs (relative)
--                          csharp may additionally declare:
--                              pinvoke_dll       string identifying the package's own native lib
--   dependencies  table    optional list of other package names

local PackageSystem = {}

local function PackageError(msg)
    error("[Axiom Packages] " .. msg, 0)
end

local k_AllowedLayers = { engine_core = true, standalone_cpp = true, csharp = true }

local function ValidateManifest(manifest, manifestPath)
    if type(manifest) ~= "table" then
        PackageError("Manifest at '" .. manifestPath .. "' did not return a table.")
    end
    if type(manifest.name) ~= "string" or manifest.name == "" then
        PackageError("Manifest at '" .. manifestPath .. "' is missing required string field 'name'.")
    end
    if type(manifest.version) ~= "string" then
        PackageError("Package '" .. manifest.name .. "' is missing required string field 'version'.")
    end
    if type(manifest.layers) ~= "table" then
        PackageError("Package '" .. manifest.name .. "' must declare a 'layers' table.")
    end

    local layerCount = 0
    for layerName, layerSpec in pairs(manifest.layers) do
        if not k_AllowedLayers[layerName] then
            PackageError("Package '" .. manifest.name .. "' declares unknown layer '" ..
                tostring(layerName) .. "'. Allowed: engine_core, standalone_cpp, csharp.")
        end
        if type(layerSpec) ~= "table" then
            PackageError("Package '" .. manifest.name .. "' layer '" .. layerName .. "' must be a table.")
        end
        if type(layerSpec.sources) ~= "table" or #layerSpec.sources == 0 then
            PackageError("Package '" .. manifest.name .. "' layer '" .. layerName ..
                "' must declare at least one source pattern in 'sources'.")
        end
        layerCount = layerCount + 1
    end

    if layerCount == 0 then
        PackageError("Package '" .. manifest.name .. "' declares zero layers; at least one is required.")
    end

    if manifest.layers.csharp and manifest.layers.csharp.pinvoke_dll then
        if not (manifest.layers.engine_core or manifest.layers.standalone_cpp) then
            PackageError("Package '" .. manifest.name ..
                "' declares 'pinvoke_dll' on csharp layer but has no native (engine_core / standalone_cpp) layer to bridge to.")
        end
    end

    if manifest.dependencies ~= nil and type(manifest.dependencies) ~= "table" then
        PackageError("Package '" .. manifest.name .. "' has invalid 'dependencies' field; expected a table or nil.")
    end
end

local function LoadManifestsFromRoot(searchRoot, manifests, byName)
    if not os.isdir(searchRoot) then
        return
    end

    local entries = os.matchdirs(path.join(searchRoot, "*"))
    for _, entry in ipairs(entries) do
        local manifestPath = path.join(entry, "axiom-package.lua")
        if os.isfile(manifestPath) then
            local manifest = dofile(manifestPath)
            ValidateManifest(manifest, manifestPath)

            if byName[manifest.name] then
                PackageError("Duplicate package name '" .. manifest.name ..
                    "' (first at '" .. byName[manifest.name].PackageDir ..
                    "', second at '" .. entry .. "').")
            end

            manifest.PackageDir = entry
            manifest.ManifestPath = manifestPath
            table.insert(manifests, manifest)
            byName[manifest.name] = manifest
        end
    end
end

local function TopoSort(manifests, byName)
    local sorted = {}
    local visiting = {}
    local visited = {}

    local function Visit(name, dependentName)
        if visited[name] then return end
        if visiting[name] then
            PackageError("Cyclic package dependency involving '" .. name .. "'.")
        end

        local m = byName[name]
        if not m then
            local dependent = dependentName and ("'" .. dependentName .. "' depends on ") or ""
            PackageError(dependent .. "missing package '" .. name .. "'.")
        end

        visiting[name] = true

        if m.dependencies then
            for _, depName in ipairs(m.dependencies) do
                Visit(depName, name)
            end
        end

        visiting[name] = nil
        visited[name] = true
        table.insert(sorted, m)
    end

    for _, m in ipairs(manifests) do
        Visit(m.name, nil)
    end

    return sorted
end

local function ResolvePaths(packageDir, patterns)
    local resolved = {}
    if patterns then
        for _, pat in ipairs(patterns) do
            table.insert(resolved, path.join(packageDir, pat))
        end
    end
    return resolved
end

local function AppendAll(dst, src)
    for _, value in ipairs(src) do
        table.insert(dst, value)
    end
end

local function RegisterNativeProject(manifest)
    local layers = manifest.layers
    local hasEngineCore = layers.engine_core ~= nil
    local hasStandalone = layers.standalone_cpp ~= nil

    if not hasEngineCore and not hasStandalone then
        return
    end

    local nativeName = "Pkg." .. manifest.name .. ".Native"

    local sources = {}
    local includes = {}

    local function AbsorbLayer(layer)
        AppendAll(sources, ResolvePaths(manifest.PackageDir, layer.sources))
        AppendAll(includes, ResolvePaths(manifest.PackageDir, layer.includes))
        AppendAll(includes, ResolvePaths(manifest.PackageDir, layer.private_includes))
    end

    if hasEngineCore then AbsorbLayer(layers.engine_core) end
    if hasStandalone then AbsorbLayer(layers.standalone_cpp) end

    project(nativeName)
        location(path.join(ROOT_DIR, "premake/generated", nativeName))
        kind "SharedLib"
        language "C++"
        cppdialect "C++20"
        cdialect "C17"
        staticruntime "off"
        warnings "Extra"

        targetdir(path.join(ROOT_DIR, "bin/" .. outputdir .. "/%{prj.name}"))
        objdir(path.join(ROOT_DIR, "bin-int/" .. outputdir .. "/%{prj.name}"))

        files(sources)
        includedirs(includes)

        if hasEngineCore then
            -- Engine-core packages link against the engine and pick up its include set.
            UseDependencySet(Dependency.EditorRuntimeCommon)
            -- Engine is a SharedLib; consumers must declare the import side of AXIOM_API.
            defines { "AIM_IMPORT_DLL" }
        end

        if manifest.dependencies then
            for _, depName in ipairs(manifest.dependencies) do
                local depNativeName = "Pkg." .. depName .. ".Native"
                links { depNativeName }
                dependson { depNativeName }
            end
        end

        filter "system:windows"
            systemversion "latest"
            buildoptions { "/utf-8" }
            defines { "AIM_PLATFORM_WINDOWS" }

        filter "system:linux"
            pic "On"
            defines { "AIM_PLATFORM_LINUX" }

        filter "configurations:Debug"
            runtime "Debug"
            symbols "On"
            defines { "AIM_DEBUG", "_DEBUG" }

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

        filter {}
end

local function RegisterCSharpProject(manifest)
    local layer = manifest.layers.csharp
    if not layer then
        return
    end

    local csharpName = "Pkg." .. manifest.name

    project(csharpName)
        location(path.join(ROOT_DIR, "premake/generated", csharpName))
        kind "SharedLib"
        language "C#"
        dotnetframework "net9.0"

        if layer.allow_unsafe then
            clr "Unsafe"
        end

        targetdir(path.join(ROOT_DIR, "bin/" .. outputdir .. "/%{prj.name}"))
        objdir(path.join(ROOT_DIR, "bin-int/" .. outputdir .. "/%{prj.name}"))

        -- Sensible defaults for Axiom packages: nullable annotations enabled, dynamic
        -- loading allowed (so the runtime can Assembly.LoadFrom them), output sits
        -- directly under bin/<config>/<Pkg.Name>/ without a net9.0/ sub-folder.
        vsprops {
            AppendTargetFrameworkToOutputPath = "false",
            Nullable = "enable",
            EnableDynamicLoading = "true",
            AllowUnsafeBlocks = layer.allow_unsafe and "true" or "false",
        }

        files(ResolvePaths(manifest.PackageDir, layer.sources))

        if manifest.dependencies then
            for _, depName in ipairs(manifest.dependencies) do
                local depCsharpName = "Pkg." .. depName
                links { depCsharpName }
                dependson { depCsharpName }
            end
        end

        filter "configurations:Debug"
            symbols "On"
            optimize "Off"
            defines { "AIM_DEBUG" }
        filter "configurations:Release"
            optimize "On"
            symbols "On"
            defines { "AIM_RELEASE" }
        filter "configurations:Dist"
            optimize "Full"
            symbols "Off"
            defines { "AIM_DIST" }
        filter {}
end

-- Reads <project>/axiom-project.json and extracts the top-level "packages" array.
-- Returns:
--   nil               — no project given (engine-developer mode, legacy scan-all)
--   {}                — project loaded, no packages installed (fresh/empty project)
--   { "A", "B", ... } — explicit allow-list
--
-- Uses pattern matching rather than a full JSON parser because the schema we generate
-- is well-formed and the field shape is trivial (top-level array of strings).
local function ReadProjectPackagesAllowList(projectRootDir)
    if not projectRootDir or projectRootDir == "" then
        return nil
    end

    local manifestPath = path.join(projectRootDir, "axiom-project.json")
    if not os.isfile(manifestPath) then
        -- A project path was specified but axiom-project.json is missing — fail closed
        -- (treat as no packages installed) rather than scanning everything.
        return {}
    end

    local file = io.open(manifestPath, "rb")
    if not file then
        return {}
    end
    local jsonText = file:read("*all")
    file:close()
    if not jsonText then
        return {}
    end

    -- Strip line comments and block comments (defensive — JSON doesn't have them but
    -- some users add them). Cheap to do.
    jsonText = jsonText:gsub("//[^\n]*", ""):gsub("/%*.-%*/", "")

    -- Locate "packages" : [ ... ]. The string up to the first matching ] is the array.
    local arrayBody = jsonText:match('"packages"%s*:%s*%[(.-)%]')
    if not arrayBody then
        -- Field absent in axiom-project.json = no packages installed.
        return {}
    end

    local packages = {}
    -- Pull every double-quoted string out of the array body. Package names contain only
    -- A-Z, a-z, 0-9, '.', '_', '-' so no escape handling needed.
    for name in arrayBody:gmatch('"([^"]*)"') do
        if name ~= "" then
            table.insert(packages, name)
        end
    end

    return packages
end

local function FilterManifestsByAllowList(manifests, byName, allowList)
    local allowed = {}
    for _, name in ipairs(allowList) do
        allowed[name] = true
    end

    local filtered = {}
    local filteredByName = {}
    local skipped = {}

    for _, manifest in ipairs(manifests) do
        if allowed[manifest.name] then
            table.insert(filtered, manifest)
            filteredByName[manifest.name] = manifest
        else
            table.insert(skipped, manifest.name)
        end
    end

    -- Detect missing dependencies — the user listed a package that isn't on disk anywhere.
    for _, name in ipairs(allowList) do
        if not filteredByName[name] then
            print("[Axiom Packages] WARNING: project lists package '" .. name ..
                "' but no manifest with that name was found under packages/ or <project>/Packages/.")
        end
    end

    if #skipped > 0 then
        print("[Axiom Packages] Skipped " .. tostring(#skipped) ..
            " package(s) not in the project's allow-list: " .. table.concat(skipped, ", "))
    end

    return filtered, filteredByName
end

function PackageSystem.LoadAll()
    local manifests = {}
    local byName = {}

    -- Engine-shipped packages live in <repo>/packages/.
    LoadManifestsFromRoot(path.join(ROOT_DIR, "packages"), manifests, byName)

    -- Project-local packages live in <project>/Packages/. The user opts in by passing
    -- --axiom-project=<absolute-path> to premake. Re-run premake when switching projects.
    local projectPath = _OPTIONS["axiom-project"]
    if projectPath and projectPath ~= "" then
        local projectPackagesRoot = path.join(projectPath, "Packages")
        if os.isdir(projectPackagesRoot) then
            print("[Axiom Packages] Including project-local packages from: " .. projectPackagesRoot)
            LoadManifestsFromRoot(projectPackagesRoot, manifests, byName)
        else
            print("[Axiom Packages] --axiom-project set but no Packages/ folder at: " .. projectPackagesRoot)
        end
    end

    -- If a project is loaded, the project's `packages` allow-list is the source of
    -- truth: only packages explicitly listed there are registered for build. Empty
    -- list / missing field means "no packages installed" (fresh project state).
    -- Without --axiom-project, we fall back to scan-everything for engine-developer
    -- workflows where there's no project context.
    if projectPath and projectPath ~= "" then
        local allowList = ReadProjectPackagesAllowList(projectPath)
        if allowList then
            print("[Axiom Packages] Project-declared package allow-list (" ..
                tostring(#allowList) .. " entries): " ..
                (#allowList > 0 and table.concat(allowList, ", ") or "<empty>"))
            manifests, byName = FilterManifestsByAllowList(manifests, byName, allowList)
        end
    end

    if #manifests == 0 then
        return
    end

    local sorted = TopoSort(manifests, byName)

    group "Packages"
    for _, manifest in ipairs(sorted) do
        RegisterNativeProject(manifest)
        RegisterCSharpProject(manifest)
    end
    group ""

    local names = {}
    for _, m in ipairs(sorted) do
        table.insert(names, m.name)
    end
    print("[Axiom Packages] Registered " .. tostring(#sorted) .. " package(s): " .. table.concat(names, ", "))
end

return PackageSystem
