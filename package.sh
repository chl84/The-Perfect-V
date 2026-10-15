#!/usr/bin/env bash
set -euo pipefail
game_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cmake -S "$game_dir" -B "$game_dir/build-vulkan" -DCMAKE_BUILD_TYPE=Release
cmake --build "$game_dir/build-vulkan" --parallel 4
mkdir -p "$game_dir/dist"
staging_dir="$(mktemp -d "$game_dir/dist/.package-XXXXXX")"
trap 'rm -rf -- "$staging_dir"' EXIT
bundle_dir="$staging_dir/ThePerfectV-linux-x86_64"
mkdir -p "$bundle_dir/docs"
cp "$game_dir/build-vulkan/the-perfect-v" "$bundle_dir/the-perfect-v"
cp "$game_dir/play.sh" "$game_dir/README.md" "$game_dir/LICENSE" "$bundle_dir/"
cp -R "$game_dir/assets" "$bundle_dir/"
cp "$game_dir/docs/DEVELOPMENT.md" "$bundle_dir/docs/"
cp "$game_dir/docs/vulkan-sdl3-migration.md" "$bundle_dir/docs/"
cp -R "$game_dir/docs/screenshots" "$bundle_dir/docs/"
# Snapshot the working files used by this build, including uncommitted changes.
# Keep local reference fixtures, captures, credentials and saves out of the source bundle.
git -C "$game_dir" ls-files -z --cached --others --exclude-standard -- \
  CMakeLists.txt LICENSE README.md .gitignore package.sh play.sh play-vulkan.sh \
  src assets tests tools docs/DEVELOPMENT.md docs/vulkan-sdl3-migration.md docs/screenshots \
  > "$staging_dir/source-files"
tar -czf "$bundle_dir/source.tar.gz" -C "$game_dir" --null -T "$staging_dir/source-files"
tar -czf "$staging_dir/ThePerfectV-linux-x86_64.tar.gz" -C "$staging_dir" ThePerfectV-linux-x86_64
rm -rf -- "$game_dir/dist/ThePerfectV-linux-x86_64"
mv "$bundle_dir" "$game_dir/dist/ThePerfectV-linux-x86_64"
mv "$staging_dir/ThePerfectV-linux-x86_64.tar.gz" "$game_dir/dist/ThePerfectV-linux-x86_64.tar.gz"
printf 'Linux package: %s\n' "$game_dir/dist/ThePerfectV-linux-x86_64.tar.gz"
