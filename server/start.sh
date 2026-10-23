#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
python="$root_dir/python/.venv/bin/python"

if [[ ! -x "$python" ]]; then
    printf '[ERROR] Python environment is missing. Run bash build.sh first.\n' >&2
    exit 1
fi

cd "$root_dir"
export PYTHONUTF8=1
exec "$python" server/app.py "$@"
