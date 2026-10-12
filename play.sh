#!/usr/bin/env bash
set -euo pipefail
game_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ -x "$game_dir/ski-jump-pro" ]]; then
  exec "$game_dir/ski-jump-pro" "$@"
fi
if [[ ! -x "$game_dir/build/ski-jump-pro" ]]; then
  cmake -S "$game_dir" -B "$game_dir/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$game_dir/build" --parallel 4
fi
exec "$game_dir/build/ski-jump-pro" "$@"
