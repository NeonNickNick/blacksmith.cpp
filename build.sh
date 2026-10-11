#!/usr/bin/env bash
set -euo pipefail
trap 'printf "[ERROR] Build failed at line %s.\n" "$LINENO" >&2' ERR

export UV_LINK_MODE=copy
root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$root_dir/out/build/linux-release"

for tool in uv cmake; do
    if ! command -v "$tool" >/dev/null; then
        printf '[ERROR] Required tool not found: %s\n' "$tool" >&2
        exit 1
    fi
done

cd "$root_dir/python/blacksmith"
printf '[INFO] uv sync (bindings)\n'
uv sync

printf '[INFO] CMake configure (Release, default compiler)\n'
cmake -S "$root_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE="$PWD/.venv/bin/python" "$@"
printf '[INFO] CMake build\n'
cmake --build "$build_dir" --config Release

printf '[INFO] Generate binding stubs\n'
uv run python -m pybind11_stubgen blacksmith.core -o ..
uv run python -m pybind11_stubgen blacksmith.master -o ..

printf '[INFO] Build wheel\n'
mkdir -p dist
wheel_dir="$(mktemp -d "$PWD/dist/linux.XXXXXX")"
uv build --wheel --out-dir "$wheel_dir"
shopt -s nullglob
wheels=("$wheel_dir"/blacksmith-*.whl)
if (( ${#wheels[@]} != 1 )); then
    printf '[ERROR] Expected one new blacksmith wheel in %s\n' "$wheel_dir" >&2
    exit 1
fi

cd "$root_dir/python"
printf '[INFO] uv sync (training)\n'
uv sync
printf '[INFO] Install wheel: %s\n' "${wheels[0]}"
uv pip install --python "$PWD/.venv/bin/python" --reinstall "${wheels[0]}"

printf '[SUCCESS] Build completed.\n'
