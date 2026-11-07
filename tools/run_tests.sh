#!/usr/bin/env bash
# Host-side tests. Packet tests run everywhere; the inline-hook test only on
# aarch64 (it executes real aarch64 instructions).
set -e
cd "$(dirname "$0")/.."
mkdir -p build
CXX=${CXX:-clang++}
echo "== packets"
$CXX -std=c++17 -Inative/src -O1 -o build/test_packets tests/test_packets.cpp native/src/packets.cpp
./build/test_packets
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
  echo "== hook ($ARCH)"
  $CXX -std=c++17 -Inative/src -O1 -o build/test_hook tests/test_hook.cpp native/src/hook.cpp
  ./build/test_hook
else
  echo "== hook skipped ($ARCH)"
fi
