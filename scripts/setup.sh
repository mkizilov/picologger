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
fi
