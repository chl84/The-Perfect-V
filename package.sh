#!/usr/bin/env bash
set -euo pipefail
game_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cmake -S "$game_dir" -B "$game_dir/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$game_dir/build" --parallel 4
mkdir -p "$game_dir/dist"
staging_dir="$(mktemp -d "$game_dir/dist/.package-XXXXXX")"
trap 'rm -rf -- "$staging_dir"' EXIT
bundle_dir="$staging_dir/ThePerfectV-linux-x86_64"
mkdir -p "$bundle_dir/docs"
cp "$game_dir/build/the-perfect-v" "$bundle_dir/the-perfect-v"
cp "$game_dir/play.sh" "$game_dir/README.md" "$bundle_dir/"
cp -R "$game_dir/assets" "$bundle_dir/"
cp "$game_dir/docs/DEVELOPMENT.md" "$bundle_dir/docs/"
tar -czf "$staging_dir/ThePerfectV-linux-x86_64.tar.gz" -C "$staging_dir" ThePerfectV-linux-x86_64
rm -rf -- "$game_dir/dist/ThePerfectV-linux-x86_64"
mv "$bundle_dir" "$game_dir/dist/ThePerfectV-linux-x86_64"
mv "$staging_dir/ThePerfectV-linux-x86_64.tar.gz" "$game_dir/dist/ThePerfectV-linux-x86_64.tar.gz"
printf 'Linux package: %s\n' "$game_dir/dist/ThePerfectV-linux-x86_64.tar.gz"
