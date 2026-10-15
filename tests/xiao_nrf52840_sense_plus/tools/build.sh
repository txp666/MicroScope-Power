#!/usr/bin/env bash
set -euo pipefail

test_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
repo_root="$(cd -- "$test_root/../.." && pwd)"
cli="${ARDUINO_CLI:-arduino-cli}"
cli_args=()
if [[ -n "${ARDUINO_CLI_CONFIG:-}" ]]; then
  cli_args+=(--config-file "$ARDUINO_CLI_CONFIG")
fi
if ! command -v "$cli" >/dev/null 2>&1; then
  echo "Install arduino-cli, or set ARDUINO_CLI to its executable path." >&2
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "Install Python 3 for UF2 conversion and verification." >&2
  exit 1
fi

build_dir="$test_root/.build"
compile_dir="$build_dir/arduino"
output_dir="$test_root/artifacts"
sketch_name=xiao_nrf52840_combined_test
verifier="$test_root/tools/verify_uf2.py"
mkdir -p "$compile_dir" "$output_dir"
# Invalidate previous success metadata before attempting a new build.
rm -f "$output_dir/validation.json" "$output_dir/SHA256SUMS"

"$cli" "${cli_args[@]}" version > "$build_dir/arduino-cli-version.txt"
"$cli" "${cli_args[@]}" core list --format json > "$build_dir/core-list.json"
python3 - "$build_dir/core-list.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    platforms = json.load(handle).get("platforms", [])
installed = next((p.get("installed_version", p.get("installed"))
                  for p in platforms if p.get("id") == "Seeeduino:nrf52"), None)
if installed != "1.1.13":
    raise SystemExit("This build requires Seeeduino:nrf52 1.1.13 (non-mbed); "
                     "installed version: " + str(installed))
PY
python3 "$verifier" --repo-root "$repo_root" \
  --snapshot-sources "$build_dir/source-inputs.json"

"$cli" "${cli_args[@]}" compile \
  --fqbn Seeeduino:nrf52:xiaonRF52840SensePlus \
  --build-property 'recipe.objcopy.uf2.pattern=python3 "{runtime.platform.path}/tools/uf2conv/uf2conv.py" -f 0xADA52840 -c -o "{build.path}/{build.project_name}.uf2" "{build.path}/{build.project_name}.hex"' \
  --build-path "$compile_dir" \
  "$test_root/firmware/$sketch_name" 2>&1 | tee "$build_dir/build.log"

cp "$compile_dir/$sketch_name.ino.uf2" \
   "$output_dir/xiao-sense-plus-combined-test.uf2"
cp "$build_dir/build.log" "$output_dir/build.log"
python3 "$verifier" --repo-root "$repo_root" --output-dir "$output_dir" \
  --source-manifest "$build_dir/source-inputs.json"
