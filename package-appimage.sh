#!/usr/bin/env bash
set -euo pipefail
game_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$game_dir"
if [[ $(uname -m) != x86_64 ]]; then
  printf 'This recipe builds x86_64 AppImages.\n' >&2
  exit 1
fi
build_root="$game_dir/build-appimage"
mkdir -p "$build_root/downloads" "$game_dir/dist"
export CC="${CC:-gcc-11}" CXX="${CXX:-g++-11}"
for tool in "$CC" "$CXX" cmake ninja pkg-config glslangValidator patchelf mksquashfs desktop-file-validate python3 curl; do
  command -v "$tool" >/dev/null || { printf 'Missing build tool: %s (use the GitHub Actions build)\n' "$tool" >&2; exit 1; }
done
python3 - "$game_dir/packaging/appimage/dependencies.json" "$build_root/downloads" <<'PY'
import hashlib,json,pathlib,subprocess,sys
for name,item in json.load(open(sys.argv[1])).items():
 path=pathlib.Path(sys.argv[2])/name
 if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:
  staged=path.with_suffix('.tmp')
  subprocess.run(['curl','--fail','--location','--retry','3','--output',str(staged),item['url']],check=True)
  if hashlib.sha256(staged.read_bytes()).hexdigest()!=item['sha256']:
   raise RuntimeError('Checksum mismatch: '+name)
  staged.replace(path)
 if name!='sdl':path.chmod(0o755)
PY
sdl_source="$build_root/SDL3-3.4.16"
if [[ ! -f "$sdl_source/CMakeLists.txt" ]]; then
  tar -xzf "$build_root/downloads/sdl" -C "$build_root"
fi
sdl_prefix="$build_root/sdl-prefix"
cmake -S "$sdl_source" -B "$build_root/sdl-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$sdl_prefix" -DCMAKE_INSTALL_LIBDIR=lib \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF \
  -DSDL_EXAMPLES=OFF -DSDL_WAYLAND_LIBDECOR=OFF -DSDL_RPATH=OFF
cmake --build "$build_root/sdl-build" --parallel "${BUILD_JOBS:-4}"
cmake --install "$build_root/sdl-build"
export PKG_CONFIG_PATH="$sdl_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$sdl_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake -S "$game_dir" -B "$build_root/game" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DSJP_REFERENCE_TESTS=OFF
cmake --build "$build_root/game" --parallel "${BUILD_JOBS:-4}"
ctest --test-dir "$build_root/game" --output-on-failure
staging_dir="$(mktemp -d "$build_root/.appimage-XXXXXX")"
trap 'rm -rf -- "$staging_dir"' EXIT
appdir="$staging_dir/ThePerfectV.AppDir"
DESTDIR="$appdir" cmake --install "$build_root/game"
cp "$game_dir/packaging/appimage/AppRun" "$appdir/AppRun"
chmod +x "$appdir/AppRun"
cp "$game_dir/packaging/appimage/the-perfect-v.desktop" "$appdir/"
cp "$game_dir/assets/icon.svg" "$appdir/the-perfect-v.svg"
ln -s the-perfect-v.svg "$appdir/.DirIcon"
desktop-file-validate "$appdir/the-perfect-v.desktop"
documentation="$appdir/usr/share/doc/the-perfect-v"
mkdir -p "$documentation/third-party"
python3 "$game_dir/packaging/appimage/bundle-libraries.py" "$appdir" "$game_dir/dist/AppImage-build.json"
cp "$game_dir/dist/AppImage-build.json" "$documentation/"
cp "$sdl_source/LICENSE.txt" "$documentation/third-party/SDL3-LICENSE.txt"
cp "$game_dir/packaging/appimage/RUNTIME-LICENSE" "$documentation/third-party/AppImage-runtime-LICENSE.txt"
# Source and build recipes travel inside the AppImage; no separate tar/zip game package.
git ls-files -z --cached --others --exclude-standard -- \
  CMakeLists.txt LICENSE README.md .gitignore package.sh package-appimage.sh package-windows.ps1 play.sh play-vulkan.sh \
  src assets tests tools packaging .github docs/DEVELOPMENT.md docs/vulkan-sdl3-migration.md docs/screenshots \
  > "$staging_dir/source-files"
tar -czf "$documentation/source.tar.gz" --null -T "$staging_dir/source-files"
cp "$build_root/downloads/sdl" "$documentation/SDL3-3.4.16.tar.gz"
app_version="${APP_VERSION:-$(git describe --tags --always --dirty)}"
if [[ ! "$app_version" =~ ^[A-Za-z0-9._+-]+$ ]]; then
  printf 'Invalid APP_VERSION: %s\n' "$app_version" >&2
  exit 1
fi
artifact="$game_dir/dist/ThePerfectV-${app_version}-x86_64.AppImage"
# Extract the packaging tool rather than depending on FUSE in the build runner.
(cd "$staging_dir" && "$build_root/downloads/appimagetool" --appimage-extract >/dev/null)
ARCH=x86_64 VERSION="$app_version" "$staging_dir/squashfs-root/AppRun" \
  --runtime-file "$build_root/downloads/runtime" --no-appstream "$appdir" "$artifact"
chmod +x "$artifact"
(cd "$game_dir/dist" && sha256sum "$(basename "$artifact")" > "$(basename "$artifact").sha256")
printf 'AppImage: %s\n' "$artifact"
