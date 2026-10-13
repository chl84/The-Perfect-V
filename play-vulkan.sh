#!/usr/bin/env bash
set -euo pipefail
game_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -x "$game_dir/build-vulkan/the-perfect-v" ]]; then
  cmake -S "$game_dir" -B "$game_dir/build-vulkan" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$game_dir/build-vulkan" --parallel 4
fi
exec "$game_dir/build-vulkan/the-perfect-v" --vulkan "$@"
