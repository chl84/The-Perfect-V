#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ $(uname -s) != Darwin ]]; then
  echo 'Build the macOS package on a Mac or through GitHub Actions.' >&2
  exit 1
fi
root=$PWD
build="$root/build-macos"
prefix="$build/dependencies"
shaders=${1:-"$build/shaders"}
cmake -S packaging/macos/dependencies -B "$build/dependency-build" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build "$build/dependency-build" --parallel 4
cmake --install "$build/dependency-build" --prefix "$prefix"
shader_args=("-DTPV_PRECOMPILED_SHADER_DIR=")
if [[ -f "$shaders/scene.vert.inc" ]]; then
  shader_args+=("-DTPV_PRECOMPILED_SHADER_DIR=$shaders")
fi
cmake -S . -B "$build/game" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DSJP_REFERENCE_TESTS=OFF "${shader_args[@]}"
cmake --build "$build/game" --parallel 4
ctest --test-dir "$build/game" --output-on-failure
stage="$build/package"
rm -rf "$stage"
cmake --install "$build/game" --prefix "$stage"
app="$stage/The Perfect V.app"
frameworks="$app/Contents/Frameworks"
resources="$app/Contents/Resources"
mkdir -p "$frameworks" "$resources/docs/third-party" "$build/downloads"
cp -L "$prefix/lib/libSDL3.0.dylib" "$frameworks/"
mvk="$build/downloads/MoltenVK-macos-1.4.2.tar"
if [[ ! -f "$mvk" ]]; then
  curl -fL --retry 3 --connect-timeout 30 --max-time 300 \
    -o "$mvk" https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-macos.tar
fi
echo "f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e  $mvk" | shasum -a 256 --check
mkdir -p "$build/moltenvk"
tar -xf "$mvk" -C "$build/moltenvk"
cp "$build/moltenvk/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" "$frameworks/"
cp "$build/moltenvk/MoltenVK/LICENSE" "$resources/docs/third-party/MoltenVK-LICENSE.txt"
cp "$build/dependency-build/_deps/sdl-src/LICENSE.txt" "$resources/docs/third-party/SDL3-LICENSE.txt"
cp "$build/dependency-build/_deps/freetype-src/docs/FTL.TXT" "$resources/docs/third-party/FreeType-LICENSE.txt"
iconset="$build/ThePerfectV.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
  sips -z "$size" "$size" packaging/macos/icon-1024.png --out "$iconset/icon_${size}x${size}.png" >/dev/null
  double=$((size * 2))
  sips -z "$double" "$double" packaging/macos/icon-1024.png --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$resources/ThePerfectV.icns"
python3 packaging/macos/package.py "$app" "$build/dependency-build/downloads"
