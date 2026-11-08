#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cmake --preset host
cmake --build --preset host
ctest --preset host
cmake --preset firmware
cmake --build --preset firmware
