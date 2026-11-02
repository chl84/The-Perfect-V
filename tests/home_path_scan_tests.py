"""Regression coverage for source and release-package path checks."""
import importlib.util
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location(
    'check_home_paths', Path(__file__).resolve().parents[1] / 'tools/check_home_paths.py')
scanner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scanner)


class HomePathTests(unittest.TestCase):
    def test_text_binary_and_encodings(self):
        for value in ('/' + 'home/alice/project/assets', '/' + 'Users/alice/project',
                      '/' + 'root/project', 'C:' + '\\Users\\Alice\\project',
                      '/' + 'Users/Åse/project'):
            for encoding in ('utf-8', 'utf-16le', 'utf-16be'):
                with self.subTest(value=value, encoding=encoding):
                    self.assertTrue(scanner.home_paths(b'\0header\0' + value.encode(encoding) + b'\0'))
        escaped = ('/' + 'home/alice/project').replace('/', '\\/').encode()
        self.assertTrue(scanner.home_paths(escaped))
        self.assertTrue(scanner.home_paths(b'%2f' + b'home%2falice%2fproject'))
        self.assertTrue(scanner.home_paths(b'C%3a%5c' + b'Users%5cAlice%5cproject'))
        self.assertFalse(scanner.home_paths(b'$HOME/.local ~/Library/Application Support @executable_path'))

    def test_chunk_boundary(self):
        value = ('/' + 'home/alice/assets').encode()
        scan = scanner.Scanner()
        scan.stream('game', io.BytesIO(b'x' * (4 * 1024 * 1024 - 4) + value))
        self.assertTrue(scan.findings)

    def test_nested_release_sources_and_upstream_exemption(self):
        value = ('/' + 'home/alice/assets').encode()
        inner = io.BytesIO()
        with tarfile.open(fileobj=inner, mode='w:gz') as archive:
            entry = tarfile.TarInfo('src/config.h')
            entry.size = len(value)
            archive.addfile(entry, io.BytesIO(value))
        outer = io.BytesIO()
        with zipfile.ZipFile(outer, 'w') as archive:
            archive.writestr('app/docs/source/ThePerfectV-source.tar.gz', inner.getvalue())
            archive.writestr('app/docs/source/SDL3-3.4.16.tar.gz', inner.getvalue())
            archive.writestr('app/SDL3.dll', value)
        outer.seek(0)
        scan = scanner.Scanner()
        scan.stream('release.zip', outer)
        labels = {label for label, _ in scan.findings}
        self.assertTrue(any('ThePerfectV-source.tar.gz!' in label for label in labels))
        self.assertIn('release.zip!app/SDL3.dll', labels)
        self.assertFalse(any('SDL3-3.4.16.tar.gz!' in label for label in labels))
        self.assertEqual(len(scan.skipped), 1)

    def test_source_excludes_ignored_data_and_checks_new_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / '.gitignore').write_text('build/\n')
            (root / 'build').mkdir()
            value = '/' + 'home/alice/assets'
            (root / 'build/local.log').write_text(value)
            (root / 'new.txt').write_text(value)
            scan = scanner.Scanner()
            scan.source(root)
            self.assertEqual({label for label, _ in scan.findings}, {'new.txt'})

    def test_symlinks_and_broken_archives(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'link'
            try:
                path.symlink_to('/' + 'home/alice/private')
            except OSError:
                self.skipTest('Symlinks unavailable on this platform')
            scan = scanner.Scanner()
            scan.path(path)
            self.assertTrue(scan.findings)
        with self.assertRaises(zipfile.BadZipFile):
            scanner.Scanner().stream('broken.zip', io.BytesIO(b'not a zip'))


if __name__ == '__main__':
    unittest.main()
