#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bin_dir="$repo_root/bin"

if [[ ! -d "$bin_dir" ]]; then
    echo "Expected bin directory at: $bin_dir" >&2
    echo "Top-level repo contents:" >&2
    ls -la "$repo_root" >&2
    echo >&2
    echo "Directories up to depth 4:" >&2
    find "$repo_root" -maxdepth 4 -type d | sort >&2
    exit 1
fi

runtime_binary="$(find "$bin_dir" -path '*/Axiom-Runtime/Axiom-Runtime' -type f | head -n 1)"

if [[ -z "${runtime_binary}" ]]; then
    echo "Axiom-Runtime binary was not found under $bin_dir" >&2
    find "$bin_dir" -maxdepth 5 -type f | sort >&2
    exit 1
fi

runtime_dir="$(dirname "$runtime_binary")"
axiom_assets_dir="$runtime_dir/AxiomAssets"

if [[ ! -d "$axiom_assets_dir" ]]; then
    echo "Expected AxiomAssets next to the runtime binary at $axiom_assets_dir" >&2
    exit 1
fi

rm -rf "$runtime_dir/Assets" "$runtime_dir/axiom-project.json"
mkdir -p "$runtime_dir/Assets/Scenes"

cat >"$runtime_dir/axiom-project.json" <<'EOF'
{
  "name": "SmokeProject",
  "engineVersion": "ci",
  "startupScene": "SampleScene",
  "lastOpenedScene": "SampleScene",
  "buildScenes": [
    "SampleScene"
  ]
}
EOF

cat >"$runtime_dir/Assets/Scenes/SampleScene.scene" <<'EOF'
{
  "version": 1,
  "name": "SampleScene",
  "entities": [
    {
      "name": "Main Camera",
      "Transform2D": {
        "posX": 0.0,
        "posY": 0.0,
        "rotation": 0.0,
        "scaleX": 1.0,
        "scaleY": 1.0
      },
      "Camera2D": {
        "orthoSize": 5.0,
        "zoom": 1.0,
        "clearR": 0.1,
        "clearG": 0.1,
        "clearB": 0.1,
        "clearA": 1.0
      }
    },
    {
      "name": "Pixel Sprite",
      "Transform2D": {
        "posX": 0.0,
        "posY": 0.0,
        "rotation": 0.0,
        "scaleX": 1.0,
        "scaleY": 1.0
      },
      "SpriteRenderer": {
        "r": 1.0,
        "g": 1.0,
        "b": 1.0,
        "a": 1.0,
        "sortOrder": 0,
        "sortLayer": 0,
        "texture": "Default/Pixel.png"
      }
    }
  ]
}
EOF

log_file="$(mktemp)"

print_log() {
    echo "---- linux smoke log ----"
    cat "$log_file"
    echo "-------------------------"
}

set +e
(
    cd "$runtime_dir"
    timeout 10s xvfb-run -a ./Axiom-Runtime >"$log_file" 2>&1
)
status=$?
set -e

if [[ $status -ne 0 && $status -ne 124 ]]; then
    echo "Smoke test process exited with status $status" >&2
    print_log
    exit $status
fi

if ! grep -q "Loaded project:" "$log_file"; then
    echo "Runtime did not load the staged smoke-test project." >&2
    print_log
    exit 1
fi

if ! grep -q "Loaded scene: SampleScene" "$log_file"; then
    echo "Runtime did not load the smoke-test scene." >&2
    print_log
    exit 1
fi

if grep -q "Failed to load default texture" "$log_file"; then
    echo "Runtime failed to load one or more default textures." >&2
    print_log
    exit 1
fi

if grep -q "AxiomAssets/Textures not found" "$log_file"; then
    echo "Runtime could not resolve AxiomAssets/Textures." >&2
    print_log
    exit 1
fi

if grep -q "Texture 'Default/" "$log_file"; then
    echo "Runtime could not resolve a default texture from the smoke-test scene." >&2
    print_log
    exit 1
fi
