#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

MINIMUM_PYTHON = (3, 10)


# ── Helpers ──────────────────────────────────────────────────────────────────

def ensure_supported_python() -> None:
    if sys.version_info < MINIMUM_PYTHON:
        required = ".".join(str(part) for part in MINIMUM_PYTHON)
        found = ".".join(str(part) for part in sys.version_info[:3])
        raise SystemExit(
            f"[Bolt Setup] Python {required}+ is required. "
            f"Found Python {found}. Please install a newer Python version and try again."
        )

def run_step(cmd, cwd, label, allow_failure=False):
    print(f"[Bolt Setup] {label}...")
    print(f"[Bolt Setup] > {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0 and not allow_failure:
        raise RuntimeError(f"Step failed ({label}) with exit code {result.returncode}.")
    return result.returncode == 0


def check_tool(name, cmd=None):
    """Return True if *name* is reachable on PATH."""
    exe = shutil.which(cmd or name)
    if exe:
        print(f"  [OK] {name} found: {exe}")
    else:
        print(f"  [!!] {name} NOT found on PATH")
    return exe is not None


def submodules_need_update(repo_root: Path) -> bool:
    """Return True if any registered submodule is missing, conflicted, or stale."""
    gitmodules = repo_root / ".gitmodules"
    if not gitmodules.exists():
        return False
    result = subprocess.run(
        ["git", "submodule", "status", "--recursive"],
        cwd=repo_root,
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        return True
    for raw_line in result.stdout.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        # "-" = missing, "+" = wrong commit checked out, "U" = conflict
        if line[0] in {"-", "+", "U"}:
            return True
    return False


def dotnet_files_present(repo_root: Path) -> bool:
    """Return True if External/dotnet already has all required files."""
    dotnet_dir = repo_root / "External" / "dotnet"
    required = [
        dotnet_dir / "nethost.h",
        dotnet_dir / "hostfxr.h",
        dotnet_dir / "coreclr_delegates.h",
        dotnet_dir / "lib" / "nethost.lib",
        dotnet_dir / "lib" / "nethost.dll",
    ]
    return all(f.is_file() for f in required)


def resolve_premake_executable(repo_root: Path):
    if platform.system() == "Windows":
        vendored = repo_root / "vendor" / "bin" / "premake5.exe"
    else:
        vendored = repo_root / "vendor" / "bin" / "premake5"
    if vendored.is_file():
        return str(vendored)
    return shutil.which("premake5")


def normalize_host_architecture(raw_arch: str | None) -> str:
    arch = (raw_arch or "").strip().lower()
    mapping = {
        "amd64": "x64",
        "x86_64": "x64",
        "x64": "x64",
        "arm64": "arm64",
        "aarch64": "arm64",
    }
    return mapping.get(arch, "")


def detect_dotnet_version(dotnet_root: Path, host_arch: str) -> str | None:
    """Find the latest installed .NET host pack version."""
    host_packs = dotnet_root / "packs" / f"Microsoft.NETCore.App.Host.win-{host_arch}"
    if not host_packs.is_dir():
        return None
    versions = sorted(
        [d.name for d in host_packs.iterdir() if d.is_dir()],
        reverse=True,
    )
    return versions[0] if versions else None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Set up Bolt dependencies and generate project files.")
    parser.add_argument(
        "--generator",
        default=os.environ.get("BOLT_PREMAKE_ACTION"),
        help="Premake action to generate (defaults to vs2022 on Windows, gmake2 elsewhere).",
    )
    parser.add_argument(
        "--dotnet-arch",
        default=os.environ.get("BOLT_DOTNET_ARCH"),
        help="Windows .NET host-pack architecture to copy (for example: x64 or arm64).",
    )
    return parser.parse_args()


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    ensure_supported_python()
    args = parse_args()
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent

    os.chdir(repo_root)
    print(f"[Bolt Setup] Repository root: {repo_root}")
    print(f"[Bolt Setup] Platform: {platform.system()}")
    print()

    os.environ["BOLT_DIR"] = str(repo_root)

    # ── 1. Dependency check ──────────────────────────────────────────────
    print("[Bolt Setup] Checking prerequisites...")
    missing = []

    if not check_tool("git"):
        missing.append("git")

    if not check_tool("python", sys.executable):
        missing.append("python")

    premake_path = resolve_premake_executable(repo_root)
    if premake_path:
        print(f"  [OK] premake5 found: {premake_path}")
    else:
        print("  [!!] premake5 NOT found (vendor/bin or PATH)")
        missing.append("premake5")

    is_windows = platform.system() == "Windows"

    if is_windows:
        dotnet_root = Path(os.environ.get("DOTNET_ROOT", r"C:\Program Files\dotnet"))
        dotnet_arch = normalize_host_architecture(args.dotnet_arch or platform.machine())
        dotnet_exe = shutil.which("dotnet")
        if dotnet_exe:
            print(f"  [OK] dotnet found: {dotnet_exe}")
        else:
            print("  [!!] dotnet NOT found on PATH")

        detected_ver = None
        if not dotnet_arch:
            print(f"  [!!] Unsupported Windows host architecture: {platform.machine()}")
            if not dotnet_files_present(repo_root):
                missing.append("dotnet-host-pack")
        else:
            print(f"  [OK] Using .NET host architecture: {dotnet_arch}")
            os.environ["BOLT_DOTNET_ARCH"] = dotnet_arch
            detected_ver = detect_dotnet_version(dotnet_root, dotnet_arch)
        if detected_ver:
            print(f"  [OK] .NET host pack found: {detected_ver}")
        else:
            print("  [!!] No .NET host pack found — setup-dotnet will fail")
            if not dotnet_files_present(repo_root):
                missing.append("dotnet-host-pack")

    print()
    if missing:
        print(f"[Bolt Setup] WARNING: Missing prerequisites: {', '.join(missing)}")
        print("[Bolt Setup] The setup will continue but some steps may fail.")
        print()

    # ── 2. Git submodules ────────────────────────────────────────────────
    run_step(
        ["git", "submodule", "sync", "--recursive"],
        repo_root, "Syncing git submodule URLs",
    )
    if submodules_need_update(repo_root):
        run_step(
            ["git", "submodule", "update", "--init", "--recursive", "--jobs", "8"],
            repo_root, "Updating git submodules",
        )
    else:
        print("[Bolt Setup] Submodules already match the recorded revisions.")

    # ── 3. Git LFS ──────────────────────────────────────────────────────
    lfs_ok = run_step(
        ["git", "lfs", "pull"], repo_root,
        "Pulling Git LFS assets", allow_failure=True,
    )
    if not lfs_ok:
        print("[Bolt Setup] Warning: git lfs pull failed or Git LFS is unavailable. Continuing...")

    # ── 4. .NET hosting files (Windows only) ─────────────────────────────
    if is_windows:
        if dotnet_files_present(repo_root):
            print("[Bolt Setup] .NET hosting files already present — skipping.")
        else:
            print("[Bolt Setup] Setting up .NET hosting files...")
            ps_script = script_dir / "setup-dotnet.ps1"
            if not ps_script.is_file():
                print("[Bolt Setup] WARNING: setup-dotnet.ps1 not found, skipping .NET setup.")
            else:
                ps_args = ["powershell", "-ExecutionPolicy", "Bypass", "-File", str(ps_script)]
                if detected_ver:
                    ps_args += ["-DotNetVersion", detected_ver]
                if dotnet_arch:
                    ps_args += ["-DotNetArch", dotnet_arch]
                run_step(ps_args, script_dir, "Copying .NET hosting files", allow_failure=True)

    # ── 5. Premake ───────────────────────────────────────────────────────
    if premake_path:
        action = args.generator or ("vs2022" if is_windows else "gmake2")
        run_step([premake_path, action], repo_root, f"Generating {action} files via Premake")
    else:
        print("[Bolt Setup] Skipping project generation — premake5 not available.")

    print()
    print("[Bolt Setup] Setup complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[Bolt Setup] ERROR: {exc}")
        raise SystemExit(1)
