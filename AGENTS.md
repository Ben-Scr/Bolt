# Axiom Engine

2D game engine in C++20 with C# scripting. OpenGL rendering, EnTT ECS, ImGui editor, Box2D physics, miniaudio audio.

## Build

- **Build system:** Premake5 generates VS2022 solutions (Windows) or gmake2 (Linux).
- **Setup:** `Setup.bat` or `python scripts/Setup.py` — inits submodules, runs premake.
- **Premake:** `./vendor/bin/premake5.exe vs2022`
- **Build (CLI):** `"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" Axiom.sln -p:Configuration=Debug -p:Platform=x64 -verbosity:minimal`
- **Startup project:** Axiom-Launcher
- **Configs:** Debug, Release, Dist. All x64.
- **Output:** `bin/{config}-windows-x64/{project}/`
- **C++ Standard:** C++20 (Box2D and Axiom-Physics use C++23)
- **New package:** `python scripts/NewPackage.py <PackageName>` scaffolds a package under `packages/<PackageName>/` with a starter `axiom-package.lua`.

## Projects

| Project | Type | Purpose |
|---------|------|---------|
| Axiom-Engine | StaticLib | Core engine — everything else links this |
| Axiom-Editor | ConsoleApp | Scene editor (single .cpp, uses ImGuiEditorSystem) |
| Axiom-Runtime | ConsoleApp | Runs built games (single .cpp) |
| Axiom-Launcher | ConsoleApp | Project launcher UI (single .cpp) |
| Axiom-ScriptCore | C# SharedLib | Managed scripting API (.NET 9.0) |
| Axiom-Sandbox | C# SharedLib | Example game project |

## Code conventions

- **Namespace:** `Axiom` for everything. Sub-namespaces rare.
- **Member variables:** `m_PascalCase` (private), `s_PascalCase` (static), `k_PascalCase` (constants). Public fields are `PascalCase` without prefix.
- **Methods:** PascalCase. Getters: `Get*`, `Is*`, `Has*`. Setters: `Set*`.
- **Templates:** `T`, `TComponent`, `TTag`, `TSystem`.
- **Headers:** `#pragma once`. Include order: pch, local engine, third-party, std.
- **Logging:** Use `AIM_CORE_*_TAG(tag, fmt, args...)` for engine internals, `AIM_*_TAG(tag, fmt, args...)` for client-facing. Uses spdlog fmt syntax, NOT string concatenation.
- **Assertions:** `AIM_ASSERT(cond, AxiomErrorCode::X, "message")`, `AIM_CORE_ASSERT(...)`.
- **Components:** Value-type structs, no inheritance. Register in `BuiltInComponentRegistration.cpp`.
- **Tags:** Empty structs (e.g. `DisabledTag`), use `requires std::is_empty_v<TTag>`.
- **Systems:** Inherit `ISystem`, implement lifecycle: `Awake`, `Start`, `Update`, `FixedUpdate`, `OnGui`, `OnDestroy`.
- **PCH:** `pch.hpp`. Files that conflict with PCH opt out via premake filter (e.g. glad.c, scripting files).
- **Export:** `AXIOM_API` macro for DLL-visible symbols.

## Key patterns

- Handle-based resources: `TextureHandle` (index+generation), `AudioHandle` (uint32 ID).
- Scene lifecycle: `SceneDefinition` (template) -> `Scene` (instance with registry + systems).
- Component access: `scene.GetComponent<T>(entity)`, `entity.GetComponent<T>()`.
- Events: `EventDispatcher(event).Dispatch<EventType>(handler)` and `Event<Args...>` pub/sub.
- Serialization: Hand-written JSON (no library). `SceneSerializer::SaveToFile/LoadFromFile`.
- Project config: `axiom-project.json` with hand-rolled JSON parsing.

## Known issues / tech debt

- `Debugging/Logger.hpp/.cpp` is dead code (superseded by `Core/Log`). Can be deleted.
- `AxiomProject.cpp` has hand-rolled JSON parsing — fragile for nested structures.
- `SoundRequest::GetHandle` is a data member named like an accessor.
- `Scene::CreateDetachedEditorScene` and `Scene::IsEditorPreview` are editor-aware methods on the core `Scene` class (Scene.hpp:25-30) — editor concerns leaking into core.
- `Application::SetPlaymodePaused` / `IsPlaymodePaused` / `IsGameInputEnabled` leak editor concepts into the core API surface (Application.hpp:106-109).
- `TextureManager::UnloadTexture` and `AudioManager::UnloadAudio` are exposed but never called — assets accumulate across scene reloads.
- `Cereal` is in the dependency list but has zero callers — `Serialization/Cereal.hpp` is dead code (only `CsprojParser.cpp` uses one rapidxml header from inside the Cereal repo).

## File structure (engine)

```
Axiom-Engine/src/
  Core/         Application, Window, Input, Time, Log, Memory, Assert, UUID
  Graphics/     Renderer2D, Texture2D, TextureManager, Shader, GizmoRenderer, OpenGL
  Scene/        Scene, SceneManager, SceneDefinition, Entity, ComponentRegistry, ISystem
  Components/   Transform2D, Camera2D, SpriteRenderer, Rigidbody2D, BoxCollider2D, AudioSource, ParticleSystem2D, Tags
  Physics/      PhysicsSystem2D, Box2DWorld, Physics2D (static query API)
  Audio/        AudioManager, Audio, AudioHandle
  Scripting/    DotNetHost, ScriptEngine, ScriptBindings, ScriptSystem (C# only — native code lives in packages)
  Serialization/ SceneSerializer, File, Path, Directory, FileWatcher
  Project/      AxiomProject, ProjectManager, LauncherRegistry
  Gui/          GuiRenderer (editor panels live in Axiom-Editor/, not here)
  Systems/      AudioUpdateSystem, GizmosDebugSystem, ParticleUpdateSystem
  Events/       AxiomEvent, KeyEvents, MouseEvents, WindowEvents, SceneEvents
  Math/         VectorMath, Trigonometry, Common, Random
  Collections/  Vec2, Vec4, Color, AABB, Viewport, Ids
  Utils/        Event, Timer, StringHelper, CommandBuffer
  Debugging/    Logger (dead code)
```

## External dependencies (all in External/ as git submodules)

GLFW, Glad, ImGui (docking), EnTT, GLM, Box2D, Axiom-Physics, spdlog, miniaudio, STB, magic_enum, Cereal (rapidxml header only — Cereal proper is unused), .NET SDK
