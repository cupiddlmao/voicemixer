# Update without losing settings

Close the mixer controller before replacing its program files. Closing just the
window hides it; use Menu/Tray → Quit, or run `linux-mixer --quit-running`.
Make sure the controller and plugin hosts have exited before replacing executables.
If an older build only hides on the first `--quit-running`, run it again.
Do not uninstall its configuration or delete its old source folder yet.

Back up your installed profile as your normal desktop user:

```bash
cp -a "$HOME/.config/PersonalAudio/Linux Mixer" \
  "$HOME/.config/PersonalAudio/Linux Mixer.backup-$(date +%Y%m%d-%H%M%S)"
```

That folder contains settings.json (devices, routes, levels, gate/compressor,
color/position, app assignments and recorder options), plugin-racks/ (plugin
chains and states), and optional plugin-runtime.json. Installation writes the
program and libraries, not this profile. Recording files stay in your chosen
recording folder. Login startup entries are also kept.

## Gentoo / KDE source-build ZIP

1. Download `linux-mixer-0.4.1-gentoo-friend.zip` and extract into a NEW folder.
2. Follow its INSTALL.txt for dependencies. New plugin hosting requires FreeType,
   ALSA and X11 development libraries in addition to Qt/PipeWire.
3. In that extracted folder, run as your normal user:
   ```bash
   bash build.sh --install
   ```
4. Launch Linux Mixer from KDE. Existing settings load automatically when using
   the same account and install location. Reselect a device only if it changed.

The default install prefix is ~/.local. If you used VOICEMIXER_PREFIX previously,
use that same value during the update so you replace the same installation.

## Arch / CachyOS source-build ZIP

Extract the new Arch ZIP into a new folder, then run `makepkg -si` as your normal
user. Accept upgrading linux-mixer over the installed package. Do not remove your
configuration folder. Launch the same installed Linux Mixer afterward.

If you previously installed manually into ~/.local and now install an Arch
package into /usr, make sure your launcher runs the new /usr/bin/linux-mixer;
otherwise the old ~/.local/bin copy may still take precedence. Both use the same
installed profile, but do not run both controllers at once.

## Ubuntu source build

Install the build dependencies listed in README.md. Extract the 0.4.1 source,
configure/build it, and install to the same prefix you used before. An existing
.deb installation should only be upgraded with a package built for that Ubuntu
release; do not install a binary built on Arch as an Ubuntu package.

## If you ran directly from the old source folder

Development launches (`./run-mixer.sh` or `./build/linux-mixer`) keep settings.json
and plugin-racks/ beside the source. A newly extracted source folder will not
inherit those files. Keep the old folder as a backup. Before the first installed
launch, copy its settings.json and plugin-racks/ into the installed profile above
ONLY if no newer installed profile exists. Copy plugin-runtime.json too if used.
Do not copy another person's device settings or overwrite a newer profile.

Windows plugin binaries, Wine prefixes and yabridge wrappers are separate from
Voicemixer. Updating the mixer does not remove them or install them for you.
Follow docs/PLUGINS.md for Windows plugin setup; each machine needs compatible
Wine/yabridge versions and its own plugin installation. If moving machines,
plugin paths in saved racks may need updating.

## Built-in update checker

Open **Menu → Check for updates** and press **Check now**. The checker fetches
only GitHub's release list, with a 256 KiB response limit and a 15-second overall
deadline. It does not download release ZIPs or install anything. **Open release
page in browser** is available when it finds a newer version; downloading there
is a separate action.

Startup checks are off by default. You may enable **Check at startup** for at
most one successful check per day. **Include preview releases** controls whether
preview releases are offered. Voicemixer currently publishes preview builds,
so leave that option enabled to receive those updates. The Menu button shows
an update label when a newer version has been found. Preferences live in
`update-preferences.ini` beside your mixer settings and are included when you
back up the profile folder.
