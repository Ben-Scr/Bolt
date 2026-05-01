# Axiom
Axiom is a lightweight C++20 2D game engine focused on performance.

## Preview

### Game

<p align="center">
  <img src="Docs/Preview/Preview_2.png" width="48%" alt="Gameplay">
  <img src="Docs/Preview/Preview_3.png" width="48%" alt="Gameplay">
</p>

### Editor

<p align="center">
  <img src="Docs/Preview/Preview-Editor.png" width="60%" alt="Level Editor">
</p>

## External Libraries / APIs
- [OpenGL](https://www.opengl.org/) - Rendering API
- [STB](https://github.com/nothings/stb) - Graphics image library
- [GLM](https://github.com/g-truc/glm) - Graphics math library
- [GLFW](https://github.com/glfw/glfw) - Windowing/input library
- [Box2D](https://github.com/erincatto/box2d) - 2D physics library
- [Axiom-Physics](https://github.com/Ben-Scr/Axiom-Physics2D) - Lightweight 2D physics library
- [ENTT](https://github.com/skypjack/entt) - ECS library
- [Miniaudio](https://github.com/mackron/miniaudio) - Multiplatform audio library

## Prerequisites
- Git with submodule support.
- Python 3.10 or newer. `scripts/Setup.py` enforces this version.
- Premake 5. Windows expects `vendor/bin/premake5.exe`; Linux expects `vendor/bin/premake5` or a `premake5` executable on `PATH`.
- Windows: Visual Studio 2022 with the MSVC v143 C++ toolset, Windows SDK, and .NET 9 SDK/runtime for scripting projects and native hosting files.
- Linux: a C++20-capable compiler, GNU Make, Python 3.10+, and native packages for GLFW/OpenGL. On Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential xorg-dev libglu1-mesa-dev xvfb
```

The repository currently has no Git LFS patterns in `.gitattributes`. Setup skips `git lfs pull` unless LFS attributes are added later. Use `--skip-lfs` to force a skip or `--require-lfs` to fail if declared LFS assets cannot be pulled.

## Clean Checkout Setup
Clone with submodules, or initialize them immediately after cloning:

```bash
git clone --recurse-submodules <repo-url> Axiom
cd Axiom
```

If the checkout already exists:

```bash
git submodule sync --recursive
git submodule update --init --recursive --jobs 8
git submodule status --recursive
```

All submodule status lines should start with a space. A leading `-`, `+`, or `U` means the submodule is missing, at a different commit, or conflicted.

## Generate Build Files
Re-run setup after pulling changes that update dependencies, `.gitmodules`, Premake files, or C# projects.

### Windows (Visual Studio 2022)

```bat
scripts\Setup.bat
```

This prefers the Windows `py` launcher when available, validates Python 3.10+, syncs and updates submodules, copies .NET hosting files from the installed .NET 9 host pack when needed, and generates Visual Studio 2022 projects with Premake.

Optional direct invocation:

```bat
py -3 scripts\Setup.py --generator vs2022
```

### Linux (GNU Make)

```bash
chmod +x scripts/Setup.sh
./scripts/Setup.sh
```

This validates prerequisites, syncs and updates submodules, and generates `gmake2` makefiles. If neither `vendor/bin/premake5` nor `premake5` on `PATH` exists, setup fails with an explicit Premake error.

Optional direct invocation:

```bash
python3 scripts/Setup.py --generator gmake2
```

### Standalone Core Profile

For a lightweight library build without the runtime application/editor stack:

```bash
python3 scripts/Setup.py --generator gmake2 --module-profile core --skip-lfs
make config=debug -j"$(nproc)" Axiom-Engine
```

On Windows:

```bat
py -3 scripts\Setup.py --generator vs2022 --module-profile core --skip-lfs
```

Core consumers should include `Axiom/Core.hpp` or the legacy lean `Axiom.hpp`. A minimal consumer source is available at `Docs/Samples/CoreConsumer/main.cpp`. `Axiom/App.hpp`, `Core/Application.hpp`, and `EntryPoint.hpp` are full-runtime APIs and require the application module profile. Setup forwards Premake module flags such as `--module-profile custom --with-render --with-audio` for advanced builds, but the current editor/application surface expects the full runtime stack.

## Build

### Windows

Open `Axiom.sln` in Visual Studio 2022 and build the desired configuration/platform, or build from a Developer PowerShell:

```powershell
msbuild Axiom.sln /m /p:Configuration=Release /p:Platform=x64
```

### Linux

```bash
make config=debug -j"$(nproc)" Axiom-Engine
make config=debug -j"$(nproc)" Axiom-Runtime
make config=debug -j"$(nproc)" Axiom-Editor
```

## Validation Commands
After setup on a clean checkout:

```bash
git status --short
git submodule status --recursive
python3 scripts/Setup.py --generator gmake2 --skip-lfs
make config=release -j"$(nproc)" Axiom-Engine Axiom-Runtime Axiom-Editor
python3 scripts/ci/runtime_smoke_test.py
```

On Windows, use:

```bat
git status --short
git submodule status --recursive
py -3 scripts\Setup.py --generator vs2022 --skip-lfs
dotnet restore Axiom.sln
```

## Generated File Hygiene
- Generated solution/project files, build folders, and local editor state are ignored. Source folders such as `Axiom-Engine/src/Debugging` and `Axiom-Engine/src/Packages` are intentionally trackable.
- To audit ignored generated files without deleting anything, run `git clean -ndX -- . ':!External/'` from the repository root. Keep submodule cleanup separate from generated-file cleanup.
- If a submodule looks dirty, inspect it first with `git submodule status --recursive` and `git -C External/<name> status --short`; do not remove submodule contents just to refresh generated files.

## Notes
- Runtime assets are copied to the runtime output directory after build (`{targetdir}/AxiomAssets`).
- Linux builds use GLFW's X11 backend via vendored GLFW sources.

![Views](https://komarev.com/ghpvc/?username=ben-scr-repo-name&label=Repo%20views&color=218a45&style=flat)
