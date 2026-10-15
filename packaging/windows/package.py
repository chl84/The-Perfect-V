"""Create a portable Windows package including matching project/dependency source."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

stage = Path(sys.argv[1]).resolve()
downloads = Path(sys.argv[2]).resolve()
root = Path(__file__).resolve().parents[2]
version = re.search(r'project\(ThePerfectV VERSION ([0-9.]+)', (root / 'CMakeLists.txt').read_text())[1]
source = stage / 'docs/source'
source.mkdir(parents=True, exist_ok=True)
files = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).decode().split('\0')
with zipfile.ZipFile(source / 'ThePerfectV-source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for name in files:
        if name:
            archive.write(root / name, name)
for name in ('SDL3-3.4.16.tar.gz', 'freetype-2.14.1.tar.xz'):
    shutil.copy2(downloads / name, source / name)
report = {
    'version': version, 'platform': 'Windows 10/11 x86_64',
    'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'compiler': 'MSVC, static C/C++ runtime', 'SDL3': '3.4.16', 'FreeType': '2.14.1',
    'host_requirement': 'Working Vulkan GPU driver',
    'sources': 'docs/source/',
}
out = root / 'dist'
out.mkdir(exist_ok=True)
(stage / 'Windows-build.json').write_text(json.dumps(report, indent=2) + '\n')
artifact = out / f'ThePerfectV-v{version}-windows-x86_64.zip'
with zipfile.ZipFile(artifact, 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            archive.write(path, path.relative_to(stage.parent))
digest = hashlib.file_digest(artifact.open('rb'), 'sha256').hexdigest()
artifact.with_suffix('.zip.sha256').write_text(f'{digest}  {artifact.name}\n')
shutil.copy2(stage / 'Windows-build.json', out)
print(f'Windows package: {artifact} ({artifact.stat().st_size} bytes)')
