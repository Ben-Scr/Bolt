param(
    [string]$DotNetVersion,
    [string]$DotNetRoot = $env:DOTNET_ROOT,
    [string]$DotNetArch = $env:AXIOM_DOTNET_ARCH
)

function Resolve-HostArchitecture([string]$Architecture) {
    if (-not $Architecture) {
        $Architecture = $env:PROCESSOR_ARCHITECTURE
    }

    switch ($Architecture.ToLowerInvariant()) {
        "amd64" { return "x64" }
        "x86_64" { return "x64" }
        "x64" { return "x64" }
        "arm64" { return "arm64" }
        "aarch64" { return "arm64" }
        default {
            Write-Error "Unsupported .NET host architecture: $Architecture"
            exit 1
        }
    }
}

$DotNetArch = Resolve-HostArchitecture $DotNetArch

# ── Resolve .NET root ────────────────────────────────────────────────────────
if (-not $DotNetRoot) {
    $DotNetRoot = "C:\Program Files\dotnet"
}

if (-not (Test-Path $DotNetRoot)) {
    Write-Error ".NET root not found at: $DotNetRoot"
    Write-Error "Install .NET or set DOTNET_ROOT environment variable."
    exit 1
}

# ── Auto-detect version if not supplied ──────────────────────────────────────
$HostPackBase = Join-Path $DotNetRoot ("packs\Microsoft.NETCore.App.Host.win-{0}" -f $DotNetArch)

if (-not $DotNetVersion) {
    if (-not (Test-Path $HostPackBase)) {
        Write-Error "No .NET host packs found at: $HostPackBase"
        Write-Error "Install the .NET runtime first."
        exit 1
    }

    $Latest = Get-ChildItem $HostPackBase -Directory |
              Sort-Object { [version]$_.Name } -Descending |
              Select-Object -First 1

    if (-not $Latest) {
        Write-Error "No .NET host pack versions found."
        exit 1
    }

    $DotNetVersion = $Latest.Name
    Write-Host "[setup-dotnet] Auto-detected .NET version: $DotNetVersion"
}

# ── Validate host pack path ─────────────────────────────────────────────────
$HostPackPath = Join-Path $HostPackBase "$DotNetVersion\runtimes\win-$DotNetArch\native"

if (-not (Test-Path $HostPackPath)) {
    Write-Error "Host pack not found at: $HostPackPath"
    Write-Error "Install the .NET $DotNetVersion runtime or adjust -DotNetVersion parameter."
    Write-Host "Available versions:"
    Get-ChildItem $HostPackBase -Directory | ForEach-Object { Write-Host "  $($_.Name)" }
    exit 1
}

# ── Resolve project paths ───────────────────────────────────────────────────
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$DestDir     = Join-Path $ProjectRoot "External\dotnet"
$LibDir      = Join-Path $DestDir "lib"

# ── Check if files are already up-to-date ────────────────────────────────────
$RequiredFiles = @(
    (Join-Path $DestDir "nethost.h"),
    (Join-Path $DestDir "hostfxr.h"),
    (Join-Path $DestDir "coreclr_delegates.h"),
    (Join-Path $LibDir  "nethost.lib"),
    (Join-Path $LibDir  "nethost.dll")
)

$AllPresent = $true
foreach ($f in $RequiredFiles) {
    if (-not (Test-Path $f)) {
        $AllPresent = $false
        break
    }
}

if ($AllPresent) {
    # Compare timestamps — skip if destination is newer than source
    $SrcDll  = Join-Path $HostPackPath "nethost.dll"
    $DestDll = Join-Path $LibDir "nethost.dll"
    if ((Get-Item $DestDll).LastWriteTime -ge (Get-Item $SrcDll).LastWriteTime) {
        Write-Host "[setup-dotnet] .NET hosting files already up-to-date — skipping."
        exit 0
    }
}

# ── Copy files ───────────────────────────────────────────────────────────────
New-Item -ItemType Directory -Force -Path $LibDir | Out-Null

# Headers
Copy-Item "$HostPackPath\nethost.h"              -Destination $DestDir -Force
Copy-Item "$HostPackPath\hostfxr.h"              -Destination $DestDir -Force
Copy-Item "$HostPackPath\coreclr_delegates.h"    -Destination $DestDir -Force

# Libraries
Copy-Item "$HostPackPath\nethost.lib" -Destination $LibDir -Force
Copy-Item "$HostPackPath\nethost.dll" -Destination $LibDir -Force

Write-Host "[setup-dotnet] Copied .NET $DotNetVersion ($DotNetArch) hosting files to External/dotnet/"
Write-Host "  Headers: nethost.h, hostfxr.h, coreclr_delegates.h"
Write-Host "  Libs:    nethost.lib, nethost.dll"
