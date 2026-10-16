"""Finish and audit a relocatable universal macOS app, then create its ZIP."""
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import zipfile

app = Path(sys.argv[1]).resolve()
downloads = Path(sys.argv[2]).resolve()
root = Path(__file__).resolve().parents[2]
version = re.search(r'project\(ThePerfectV VERSION ([0-9.]+)', (root / 'CMakeLists.txt').read_text())[1]
resources = app / 'Contents/Resources'
source = resources / 'docs/source'
source.mkdir(parents=True, exist_ok=True)
files = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).decode().split('\0')
with zipfile.ZipFile(source / 'ThePerfectV-source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for name in files:
        if name:
            archive.write(root / name, name)
for name in ('SDL3-3.4.16.tar.gz', 'freetype-2.14.1.tar.xz'):
    shutil.copy2(downloads / name, source / name)
report = {
    'version': version, 'platform': 'macOS 12.0+, Apple Silicon and Intel',
    'architectures': ['arm64', 'x86_64'],
    'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'compiler': 'Apple Clang', 'SDL3': '3.4.16', 'FreeType': '2.14.1', 'MoltenVK': '1.4.2',
    'MoltenVK_source': 'https://github.com/KhronosGroup/MoltenVK/tree/v1.4.2',
    'host_requirement': 'Metal-capable GPU', 'sources': 'Contents/Resources/docs/source/',
    'signing': 'Ad-hoc signature; not Developer ID signed or notarized',
}
(resources / 'macOS-build.json').write_text(json.dumps(report, indent=2) + '\n')
with (app / 'Contents/Info.plist').open('rb') as handle:
    info = plistlib.load(handle)
assert info['CFBundleShortVersionString'] == version
assert info['LSMinimumSystemVersion'] == '12.0'
binaries = sorted((app / 'Contents/Frameworks').glob('*.dylib'))
executable = app / 'Contents/MacOS' / info['CFBundleExecutable']
for binary in [*binaries, executable]:
    arches = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).split()
    if set(arches) != {'arm64', 'x86_64'}:
        raise RuntimeError(f'Not a universal binary: {binary}: {arches}')
    deps = subprocess.check_output(['otool', '-L', str(binary)], text=True)
    for line in deps.splitlines():
        if not line.startswith('\t'):
            continue
        dependency = line.strip().split(' (')[0]
        if dependency.startswith(('/System/Library/', '/usr/lib/')):
            continue
        if dependency.startswith('@rpath/') and (app / 'Contents/Frameworks' / dependency[7:]).is_file():
            continue
        raise RuntimeError(f'Unbundled dependency in {binary}: {dependency}')
    subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', str(binary)], check=True)
subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', str(app)], check=True)
subprocess.run(['codesign', '--verify', '--deep', '--strict', '--verbose=2', str(app)], check=True)
out = root / 'dist'
out.mkdir(exist_ok=True)
artifact = out / f'ThePerfectV-v{version}-macos-universal.zip'
subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app), str(artifact)], check=True)
with artifact.open('rb') as handle:
    digest = hashlib.file_digest(handle, 'sha256').hexdigest()
artifact.with_suffix('.zip.sha256').write_text(f'{digest}  {artifact.name}\n')
shutil.copy2(resources / 'macOS-build.json', out)
print(f'macOS package: {artifact} ({artifact.stat().st_size} bytes)')
