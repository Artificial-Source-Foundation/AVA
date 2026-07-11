#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
  printf 'Run this script with sudo:\n  sudo %s\n' "$0" >&2
  exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
  printf 'This installer expects an Ubuntu/Debian system with apt-get.\n' >&2
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y software-properties-common
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get update
apt-get install -y gcc-16 g++-16

printf '\nInstalled compiler versions:\n'
gcc-16 --version
g++-16 --version

printf '\nBuild AVA with GCC 16 using:\n'
printf '  cmake -S . -B build-gcc16 -G Ninja -DAVA_BUILD_TESTS=ON -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16\n'
printf '  cmake --build build-gcc16\n'
printf '  ctest --test-dir build-gcc16 --output-on-failure\n'
