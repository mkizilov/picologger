#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
root=$(pwd)
PATH="$root/.venv/bin:$PATH"
export PATH
export PYTHONDONTWRITEBYTECODE=1
if [ -z "${MPLCONFIGDIR:-}" ]; then
    mkdir -p .agent-local
    MPLCONFIGDIR=$(mktemp -d "$root/.agent-local/mpl.XXXXXXXX")
    trap 'rm -rf "$MPLCONFIGDIR"' EXIT HUP INT TERM
fi
export MPLCONFIGDIR
test -x .venv/bin/clang-tidy || { echo "Run scripts/setup.sh first" >&2; exit 1; }
cmake -S . -B build/host-sanitized -DPICOLOGGER_HOST=ON -DPICOLOGGER_SANITIZE=ON \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DFETCHCONTENT_SOURCE_DIR_UNITY=
cmake --build build/host-sanitized --parallel 2
ctest --test-dir build/host-sanitized --parallel 2 --output-on-failure
for board in pico pico_w; do
    cmake -S . -B "build/$board" -DPICOLOGGER_HOST=OFF -DPICO_BOARD="$board" \
        -DPICO_SDK_PATH="$root/third_party/pico-sdk" -DPICO_TOOLCHAIN_PATH="$root/.toolchain/arm-gnu" \
        -Dpicotool_DIR="$root/.toolchain/picotool/lib/cmake/picotool" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DFETCHCONTENT_SOURCE_DIR_PICOLOGGER_FATFS=
    cmake --build "build/$board" --parallel 2
done
.venv/bin/python scripts/run_clang_tidy.py
MPLBACKEND=Agg .venv/bin/python -m unittest discover -s test -p 'test_logger_cli.py'
.venv/bin/python -m pytest -q tests
