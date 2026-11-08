#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
root=$(pwd)
version=13.3.rel1
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) host=darwin-arm64 ;;
    Darwin-x86_64) host=darwin-x86_64 ;;
    Linux-x86_64) host=x86_64 ;;
    Linux-aarch64|Linux-arm64) host=aarch64 ;;
    *) echo "Unsupported host: $(uname -s) $(uname -m)" >&2; exit 1 ;;
esac
git submodule update --init --recursive
python3 -m venv .venv
if [ -x .venv/bin/cmake ]; then
    PATH="$root/.venv/bin:$PATH"
    export PATH
elif ! command -v cmake >/dev/null 2>&1; then
    .venv/bin/python -m pip install cmake==3.31.10
    PATH="$root/.venv/bin:$PATH"
    export PATH
fi
if [ ! -x .toolchain/arm-gnu/bin/arm-none-eabi-gcc ]; then
    mkdir -p .toolchain
    work=$(mktemp -d "$root/.toolchain/download.XXXXXXXX")
    trap 'rm -rf "$work"' EXIT HUP INT TERM
    archive="arm-gnu-toolchain-${version}-${host}-arm-none-eabi.tar.xz"
    url="https://developer.arm.com/-/media/Files/downloads/gnu/${version}/binrel/${archive}"
    curl -fL --retry 3 "$url" -o "$work/$archive"
    mkdir "$work/unpacked"
    tar -xJf "$work/$archive" -C "$work/unpacked" --strip-components=1
    test -x "$work/unpacked/bin/arm-none-eabi-gcc"
    mv "$work/unpacked" .toolchain/arm-gnu
    rm -rf "$work"
    trap - EXIT HUP INT TERM
fi
if [ ! -x .toolchain/picotool/bin/picotool ]; then
    if [ -e .toolchain/picotool ]; then
        echo "Incomplete .toolchain/picotool exists; inspect it before retrying" >&2
        exit 1
    fi
    mkdir -p .toolchain
    work=$(mktemp -d "$root/.toolchain/picotool.XXXXXXXX")
    trap 'rm -rf "$work"' EXIT HUP INT TERM
    git clone --branch 2.1.1 --depth 1 https://github.com/raspberrypi/picotool.git "$work/source"
    test "$(git -C "$work/source" rev-parse HEAD)" = de8ae5ac334e1126993f72a5c67949712fd1e1a4
    cmake -S "$work/source" -B "$work/build" -DPICO_SDK_PATH="$root/third_party/pico-sdk" \
        -DPICOTOOL_NO_LIBUSB=1 -DCMAKE_INSTALL_PREFIX="$work/install" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$work/build" --parallel 2
    cmake --install "$work/build"
    mv "$work/install" .toolchain/picotool
    rm -rf "$work"
    trap - EXIT HUP INT TERM
fi
case "$(.toolchain/picotool/bin/picotool version)" in
    "picotool v2.1.1 "*) ;;
    *) echo "Expected local picotool 2.1.1; inspect .toolchain/picotool" >&2; exit 1 ;;
esac
