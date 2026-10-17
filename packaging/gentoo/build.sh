#!/usr/bin/env bash
# Build the bundled release locally. No automatic dependency installs or audio changes.
set -euo pipefail
kit_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mode="${1:-build}"
case "$mode" in
  build|--check|--install) ;;
  -h|--help) printf 'Usage: bash build.sh [--check|--install]\nDefault: build and test. --install also installs under ~/.local.\n'; exit 0 ;;
  *) printf 'Unknown option: %s\n' "$mode" >&2; exit 2 ;;
esac
if (( EUID == 0 )); then printf 'Run this script as your normal desktop user, not root.\n' >&2; exit 1; fi
missing=0
for tool in cmake ctest ninja cc c++ pkg-config sha256sum tar pipewire pw-cli pw-dump pw-link pw-record pw-play wpctl pactl ffmpeg ffprobe; do
  if ! command -v "$tool" >/dev/null 2>&1; then printf 'Missing command: %s\n' "$tool" >&2; missing=1; fi
done
if (( missing )); then printf 'See INSTALL.txt for Gentoo dependencies and USE flags.\n' >&2; exit 1; fi
if ! pkg-config --exists Qt6Widgets; then printf 'Qt 6 Widgets development files missing; see dev-qt/qtbase:6[gui,widgets] in INSTALL.txt.\n' >&2; exit 1; fi
plugins_found=0
for dir in /usr/lib/ladspa /usr/lib64/ladspa /usr/lib/x86_64-linux-gnu/ladspa; do
  if [[ -r "$dir/ZamGate-ladspa.so" && -r "$dir/ZamComp-ladspa.so" ]]; then plugins_found=1; break; fi
done
if (( ! plugins_found )); then printf 'ZamGate/ZamComp LADSPA plugins missing; install media-plugins/zam-plugins.\n' >&2; exit 1; fi
if ! ffmpeg -hide_banner -encoders 2>/dev/null | grep 'libmp3lame' >/dev/null; then
  printf 'Note: MP3 recording needs FFmpeg with the lame USE flag. Other formats may still work.\n' >&2
fi
if [[ "$mode" == --check ]]; then printf 'Build/runtime files found. Use pactl info and wpctl status to check the active audio session.\n'; exit 0; fi
cd -- "$kit_dir"
sha256sum -c SOURCE.sha256
source_archive="$(awk 'NR==1 { print $2 }' SOURCE.sha256)"
case "$source_archive" in linux-mixer-*-source.tar.gz) ;; *) printf 'Unexpected source filename.\n' >&2; exit 1 ;; esac
version="${source_archive#linux-mixer-}"; version="${version%-source.tar.gz}"
source_dir="$kit_dir/linux-mixer-$version"
if [[ ! -d "$source_dir" ]]; then tar -xzf "$source_archive"; fi
install_prefix="${VOICEMIXER_PREFIX:-$HOME/.local}"
if [[ "$install_prefix" != /* ]]; then printf 'VOICEMIXER_PREFIX must be an absolute path.\n' >&2; exit 1; fi
jobs="${VOICEMIXER_JOBS:-2}"
if [[ ! "$jobs" =~ ^[1-9][0-9]*$ ]]; then printf 'VOICEMIXER_JOBS must be a positive integer.\n' >&2; exit 1; fi
cmake -S "$source_dir" -B "$kit_dir/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install_prefix" -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$kit_dir/build" --parallel "$jobs"
ctest --test-dir "$kit_dir/build" --output-on-failure
if [[ "$mode" == --install ]]; then
  cmake --install "$kit_dir/build"
  printf '\nInstalled. Launch from KDE as Linux Mixer, or run:\n  %q\n' "$install_prefix/bin/linux-mixer"
else
  printf '\nBuild complete. Install for your user with:\n  bash %q --install\n' "$kit_dir/build.sh"
fi
