#!/usr/bin/env python3
"""Reject absolute home paths in source files and unpacked release payloads."""
import argparse
import io
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import zipfile


HOME = re.compile(
    rb"(?:/(?:home|users|root)/|[a-z]:[\\/]+users[\\/]+)[^\x00-\x20/\\\"'<>:]+", re.I
)
ENCODED_HOME = re.compile(
    rb"(?:%2f(?:home|users|root)%2f|[a-z](?::|%3a)%5cusers%5c)[a-z0-9_.@~+-]+", re.I
)
ARCHIVES = ('.zip', '.tar', '.tar.gz', '.tgz', '.tar.xz', '.tar.bz2')
# These upstream sources contain documented example home directories. Their
# compiled libraries remain subject to the same checks as the game executable.
THIRD_PARTY_SOURCE = re.compile(r'(?:SDL3-[0-9.]+\.tar\.gz|freetype-[0-9.]+\.tar\.xz)\Z')


def home_paths(data):
    # Handle escaped JSON slashes and both UTF-16 byte orders in binaries.
    normalized = re.sub(rb'[\\/]+', b'/', data.replace(b'\x00', b''))
    return sorted({m.group().decode('utf-8', 'replace') for pattern in (HOME, ENCODED_HOME)
                   for m in pattern.finditer(normalized)})


class Scanner:
    def __init__(self):
        self.findings = set()
        self.checked = 0
        self.skipped = set()

    def check_bytes(self, label, data):
        for match in home_paths(data):
            self.findings.add((label, match))

    def stream(self, label, handle, depth=0):
        self.check_bytes(label, label.encode('utf-8'))
        name = label.rsplit('!', 1)[-1].replace('\\', '/').rsplit('/', 1)[-1]
        if THIRD_PARTY_SOURCE.fullmatch(name):
            self.skipped.add(label)
            return
        self.checked += 1
        if name.lower().endswith(ARCHIVES):
            if depth >= 8:
                raise ValueError('Archive nesting exceeds eight levels: ' + label)
            # Read nested archives without extracting files or following links.
            content = io.BytesIO(handle.read())
            if name.lower().endswith('.zip'):
                with zipfile.ZipFile(content) as archive:
                    for entry in archive.infolist():
                        child = label + '!' + entry.filename
                        self.check_bytes(child, entry.filename.encode('utf-8'))
                        if not entry.is_dir():
                            with archive.open(entry) as member:
                                self.stream(child, member, depth + 1)
            else:
                with tarfile.open(fileobj=content, mode='r:*') as archive:
                    for entry in archive:
                        child = label + '!' + entry.name
                        self.check_bytes(child, (entry.name + '\n' + entry.linkname).encode('utf-8'))
                        if entry.isfile():
                            with archive.extractfile(entry) as member:
                                self.stream(child, member, depth + 1)
            return
        if name.lower().endswith('.appimage'):
            raise ValueError('Extract the AppImage and scan its payload directory: ' + label)
        tail = b''
        while True:
            block = handle.read(4 * 1024 * 1024)
            if not block:
                break
            self.check_bytes(label, tail + block)
            tail = block[-1024:]

    def path(self, path, label=None):
        path = Path(path)
        label = label if label is not None else path.name
        self.check_bytes(label, label.encode('utf-8'))
        if path.is_symlink():
            self.check_bytes(label, str(path.readlink()).encode('utf-8'))
        elif path.is_dir():
            for child in sorted(path.iterdir()):
                self.path(child, label + '/' + child.name)
        else:
            with path.open('rb') as handle:
                self.stream(label, handle)

    def source(self, root):
        root = Path(root)
        # Include new source files while testing a working tree, while respecting
        # Git's exclusions for build directories, captures and personal data.
        names = subprocess.check_output(
            ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=root
        ).decode('utf-8').split('\0')
        for name in sorted(set(names) - {''}):
            self.path(root / name, name)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, help='scan Git-tracked and new source files')
    parser.add_argument('paths', type=Path, nargs='*', help='package ZIP/TAR files or payload directories')
    args = parser.parse_args(argv)
    if args.source_root is None and not args.paths:
        parser.error('specify --source-root or at least one package path')
    scanner = Scanner()
    try:
        if args.source_root is not None:
            scanner.source(args.source_root)
        for path in args.paths:
            scanner.path(path)
    except (OSError, ValueError, subprocess.CalledProcessError, tarfile.TarError,
            zipfile.BadZipFile, RuntimeError) as error:
        print('Home-path scan failed: ' + str(error), file=sys.stderr)
        return 1
    for label, match in sorted(scanner.findings):
        print(f'{label}: absolute home path {match}', file=sys.stderr)
    print(f'Home-path scan: {scanner.checked} files/members checked, '
          f'{len(scanner.skipped)} upstream source archives skipped, '
          f'{len(scanner.findings)} findings.')
    return int(bool(scanner.findings))


if __name__ == '__main__':
    sys.exit(main())
