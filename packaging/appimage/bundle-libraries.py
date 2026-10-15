#!/usr/bin/env python3
"""Bundle window-system libraries, retain host drivers, and audit the ELF baseline."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

appdir = Path(sys.argv[1]).resolve()
report_path = Path(sys.argv[2])
library_dir = appdir / "usr/lib"
library_dir.mkdir(parents=True, exist_ok=True)
notices = appdir / "usr/share/doc/the-perfect-v/third-party"
notices.mkdir(parents=True, exist_ok=True)
# Base OS/toolchain and graphics drivers stay on the host. The build uses GCC 11
# and glibc 2.35, verified below, rather than copying the build host's libc/driver.
host_libraries = re.compile(r"^(ld-linux.*|lib(c|m|dl|pthread|rt|resolv|util|gcc_s|stdc\+\+)\.so.*|lib(GL|EGL|GLX|GLdispatch|vulkan|drm|gbm).*\.so.*)$")
window_libraries = [
    "libX11.so.6", "libXext.so.6", "libXrandr.so.2", "libXcursor.so.1",
    "libXi.so.6", "libXfixes.so.3", "libXrender.so.1", "libXss.so.1",
    "libXtst.so.6", "libXinerama.so.1",
    "libwayland-client.so.0", "libwayland-cursor.so.0", "libwayland-egl.so.1",
    "libxkbcommon.so.0",
]

def output(*args):
    return subprocess.check_output(args, text=True, env=os.environ)

cache = {}
for line in output("ldconfig", "-p").splitlines():
    match = re.match(r"\s*(\S+) \([^)]*x86-64[^)]*\) => (\S+)", line)
    if match:
        cache.setdefault(match[1], Path(match[2]))
queue = [appdir / "usr/bin/the-perfect-v"]
queue += [cache[name] for name in window_libraries]
seen = set()
bundled = {}
packages = set()
while queue:
    binary = queue.pop()
    for line in output("ldd", str(binary)).splitlines():
        match = re.match(r"\s*(\S+) => (\S+)", line)
        if not match:
            continue
        name, location = match.groups()
        if location == "not":
            raise RuntimeError(f"Unresolved dependency in {binary}: {line}")
        if not host_libraries.match(name) and name not in seen:
            seen.add(name)
            origin = Path(location).resolve()
            destination = library_dir / name
            shutil.copy2(origin, destination)
            bundled[name] = str(origin)
            queue.append(destination)
    # The explicit window-system entry points are dlopened by SDL, not DT_NEEDED.
    if binary.name in window_libraries and binary.name not in seen:
        seen.add(binary.name)
        destination = library_dir / binary.name
        shutil.copy2(binary.resolve(), destination)
        bundled[binary.name] = str(binary.resolve())
        queue.append(destination)

for name, origin in bundled.items():
    # SDL is built by this project; its license/source is added by the shell script.
    if name.startswith("libSDL3"):
        continue
    owners = subprocess.run(["dpkg-query", "-S", "*/" + Path(origin).name],
                            text=True, capture_output=True, check=True).stdout
    for line in owners.splitlines():
        package = line.split(": ", 1)[0].split(":", 1)[0]
        notice = Path("/usr/share/doc") / package / "copyright"
        if notice.is_file() and package not in packages:
            shutil.copy2(notice, notices / (package + ".copyright"))
            packages.add(package)

elf_files = [appdir / "usr/bin/the-perfect-v", *sorted(library_dir.iterdir())]
required_glibc = (0, 0)
required_glibcxx = (0, 0, 0)
for binary in elf_files:
    subprocess.run(["patchelf", "--set-rpath",
                    "$ORIGIN/../lib" if binary.parent.name == "bin" else "$ORIGIN", str(binary)], check=True)
    versions = output("readelf", "--version-info", str(binary))
    for version in re.findall(r"\bGLIBC_(\d+\.\d+(?:\.\d+)?)", versions):
        required_glibc = max(required_glibc, tuple(map(int, version.split("."))))
    for version in re.findall(r"\bGLIBCXX_(\d+\.\d+\.\d+)", versions):
        required_glibcxx = max(required_glibcxx, tuple(map(int, version.split("."))))
if required_glibc > (2, 35) or required_glibcxx > (3, 4, 29):
    raise RuntimeError(f"Build exceeds Ubuntu 22.04 baseline: GLIBC {required_glibc}, GLIBCXX {required_glibcxx}")
report = {
    "baseline": "Ubuntu 22.04 x86_64",
    "glibc_required": ".".join(map(str, required_glibc)),
    "glibcxx_required": ".".join(map(str, required_glibcxx)),
    "compiler": output(os.environ.get("CXX", "g++"), "--version").splitlines()[0],
    "bundled_libraries": sorted(bundled),
    "host_components": ["glibc", "GCC C++ runtime", "Vulkan loader and GPU driver", "audio services"],
    "license_packages": sorted(packages),
}
report_path.write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
