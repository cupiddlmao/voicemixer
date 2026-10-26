# Complete profile backup and restore

Use **Menu → Back up complete profile…** to save one JSON file containing mixer
settings, all eight input plugin chains, plugin order/bypass and saved plugin
states. Running racks acknowledge an explicit fresh state-save request before
the export is written. A canceled, timed-out or failed snapshot does not produce
a silently stale export. Inactive racks use their last saved state.

The bundle includes device selections, routing, app rules, voice processing,
EQ, FX, faders and recorder options. It does **not** include plugin binaries,
installers, licenses, Wine prefixes/runtime configuration, recorded audio,
autostart files or update-check preferences. Those remain machine-specific.

Use **Menu → Restore profile / preset…** to restore a bundle or an existing
version 1/2/3 JSON mixer preset. The review dialog shows missing devices and
plugins and lets you map each of the five hardware inputs and five outputs to
an available device or Unassigned. Full bundles select saved devices when
available; missing selections fall back to the corresponding current device
when available. Legacy presets default to current devices. Changed input-device
mappings use mono; select a particular interface channel afterward if needed.

Plugin paths are checked directly, then by bundle/library basename in common
Linux VST3, yabridge and LADSPA directories. Availability is not a compatibility
or licensing test. A missing plugin disables its whole rack while retaining its
chain and state; install it and restore again, or disable plugin restoration to
restore only mixer settings. Import plugin chains only from a source you trust:
the selected installed plugins run native code. A partly loaded rack refuses
to overwrite its saved chain until all plugins can load.

Restore validates JSON structure, settings types/array sizes, supported plugin
XML and saved-state encoding before applying changes. Limits: 64 MiB per bundle,
32 plugins per input and 4 MiB of decoded state per plugin. Unknown bundle
versions and malformed data are rejected. Legacy presets retain plugin files
and preserve unspecified current settings.

Before applying, current plugin state is saved and workers close gracefully.
The application backs up affected files under
`~/.config/PersonalAudio/Linux Mixer/profile-backups/<timestamp>-<id>/`, then
writes the new files atomically one at a time. A write failure rolls back writes
already completed. The backup contains `settings.json` and the affected
`plugin-racks/input*/rack.json` files. Keep backups private: states can contain
plugin-specific paths or other data. To recover manually, quit the mixer, copy
those backed-up files to their original relative paths in the profile folder,
and reopen it. Backups are not automatically deleted.

Restore is blocked during recording/finalization. Canceled review does not alter
the live configuration. Successful restore preserves whether the engine was
enabled, reloads plugin workers and reconciles routing; device or Composite
topology changes may briefly restart the audio engine. Plugin compatibility
failures use the existing rack fallback/diagnostics; filesystem rollback cannot
make an incompatible plugin work. Sudden process termination/power loss during
a multi-file restore requires manual recovery from its backup.

Checks:

```sh
ctest --test-dir build --output-on-failure
python3 tests/verify_profile_private.py --xvfb /path/to/Xvfb
```

The second command uses a separate PipeWire server/profile/display and a test
VST3; it does not route anything to physical audio devices.
