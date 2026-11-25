#!/usr/bin/env python3
"""Create a personal-data-free source kit, then a native Debian package."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile

ROOT=Path(__file__).resolve().parents[1]
# Single source of truth: the CMake project version. A hardcoded literal here is how a
# 0.5.0 tree ended up publishing archives named 0.4.1.
VERSION=re.search(r'project\(LinuxMixer VERSION (\S+) ',(ROOT/'CMakeLists.txt').read_text()).group(1)
if f"pkgver={VERSION}" not in (ROOT/'packaging/arch/PKGBUILD').read_text():
    sys.exit('packaging/arch/PKGBUILD pkgver does not match the CMake project version')
parser=argparse.ArgumentParser()
parser.add_argument('--source-only',action='store_true')
args=parser.parse_args()
out=ROOT/'dist';out.mkdir(exist_ok=True)
source=out/f'linux-mixer-{VERSION}-source.tar.gz'
# Explicit allowlist: no settings, recordings, runtime logs, local SDK, credentials or user files.
def source_info(info):
    if '/__pycache__' in info.name:return None
    info.uid=info.gid=0;info.uname=info.gname='root'
    return info
with tarfile.open(source,'w:gz') as archive:
    for name in ('CMakeLists.txt','README.md','LICENSE','run-mixer.sh','native','tests','packaging','vendor','docs'):
        archive.add(ROOT/name,arcname=f'linux-mixer-{VERSION}/{name}',filter=source_info)
digest=hashlib.sha256(source.read_bytes()).hexdigest()
arch=out/'arch';arch.mkdir(exist_ok=True)
recipe=(ROOT/'packaging/arch/PKGBUILD').read_text().replace("sha256sums=('SKIP')",f"sha256sums=('{digest}')")
(arch/'PKGBUILD').write_text(recipe)
shutil.copy2(source,arch/source.name)
if not args.source_only:
    subprocess.run(['cmake','-S',str(ROOT),'-B',str(ROOT/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release'],check=True)
    subprocess.run(['cmake','--build',str(ROOT/'build'),'-j2'],check=True)
    subprocess.run(['ctest','--test-dir',str(ROOT/'build'),'--output-on-failure'],check=True)
    subprocess.run(['cpack','--config',str(ROOT/'build/CPackConfig.cmake'),'-B',str(out)],check=True,cwd=ROOT)
# One sendable archive per target, with installation instructions and matching source.
import zipfile
for target in (('arch',) if args.source_only else ('ubuntu26.04','arch')):
    with zipfile.ZipFile(out/f'linux-mixer-{VERSION}-{target}-friend.zip','w',compression=zipfile.ZIP_STORED) as bundle:
        bundle.write(source,source.name)
        if target=='arch':
            bundle.write(arch/'PKGBUILD','PKGBUILD')
            instructions=(ROOT/'docs/ARCH-INSTALL.txt').read_text()
        else:
            packages=sorted(out.glob(f'linux-mixer-{VERSION}-ubuntu26.04-*.deb'))
            if not packages:
                # Before this check the glob silently matched nothing and the zip was written
                # anyway, so a release could ship an Ubuntu kit with no Ubuntu package in it.
                found=', '.join(sorted(f.name for f in out.glob('linux-mixer-*.deb'))) or 'none'
                sys.exit(f'No linux-mixer-{VERSION}-ubuntu26.04-*.deb in {out} (found: {found}). '
                         'The Ubuntu asset has to be built on Ubuntu 26.04, or inside a container '
                         'of it: a .deb built without dpkg-shlibdeps has no dependency list and '
                         'CPack would default its architecture to i386.')
            for f in packages:bundle.write(f,f.name)
            instructions='For Ubuntu 26.04 amd64. In the extracted folder run:\n  sudo apt install ./linux-mixer-*.deb\nThen launch Linux Mixer and choose your own mic/headphone devices.\nOther Ubuntu releases need a build for their version.\n'
        bundle.write(ROOT/'docs/UPDATING.md','UPDATING.md')
        bundle.writestr('INSTALL.txt',instructions+'\nThe source archive includes documentation, source and third-party licenses.\n')
subprocess.run([sys.executable, str(ROOT/'packaging/gentoo/bundle.py'), '--version', VERSION], check=True)
checks=[]
for f in sorted(out.glob(f'linux-mixer-{VERSION}-*')):
    if f.suffix in ('.gz','.deb','.zip'):checks.append(f'{hashlib.sha256(f.read_bytes()).hexdigest()}  {f.name}\n')
(out/'SHA256SUMS').write_text(''.join(checks))
print(f'Release files: {out}')
