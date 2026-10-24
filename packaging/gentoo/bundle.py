#!/usr/bin/env python3
"""Wrap an existing release source archive without changing published release assets."""
import argparse
import hashlib
from pathlib import Path
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--version', default='0.4.0')
parser.add_argument('--source', type=Path)
args = parser.parse_args()
version = args.version
if not version or any(c not in '0123456789.' for c in version):
    parser.error('version must contain only digits and dots')
source = args.source or ROOT / 'dist' / f'linux-mixer-{version}-source.tar.gz'
expected_name = f'linux-mixer-{version}-source.tar.gz'
if source.name != expected_name:
    parser.error(f'source must be named {expected_name}')
with tarfile.open(source, 'r:gz') as archive:
    cmake = archive.extractfile(f'linux-mixer-{version}/CMakeLists.txt').read()
    if f'project(LinuxMixer VERSION {version} '.encode() not in cmake:
        parser.error('source version does not match requested bundle version')
    for member in archive.getmembers():
        parts = Path(member.name).parts
        if not parts or parts[0] != f'linux-mixer-{version}' or '..' in parts:
            parser.error('unexpected source archive member path')
        if member.name.endswith('/settings.json') or '/.runtime/' in member.name or '/.deps/' in member.name:
            parser.error('personal state found in source archive')
out = ROOT / 'dist'
out.mkdir(exist_ok=True)
path = out / f'linux-mixer-{version}-gentoo-friend.zip'
with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_STORED) as bundle:
    bundle.write(source, expected_name)
    bundle.write(ROOT / 'packaging/gentoo/build.sh', 'build.sh')
    bundle.write(ROOT / 'docs/GENTOO-INSTALL.txt', 'INSTALL.txt')
    bundle.write(ROOT / 'docs/UPDATING.md', 'UPDATING.md')
    bundle.writestr('SOURCE.sha256', f'{hashlib.sha256(source.read_bytes()).hexdigest()}  {expected_name}\n')
path.with_suffix('.zip.sha256').write_text(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n')
print(path)
