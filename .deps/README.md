The prototype loads `librnnoise_ladspa.so` from this directory.

Source: https://github.com/werman/noise-suppression-for-voice
Release: v1.10, official `linux-rnnoise.zip` Linux LADSPA binary.
Download: https://github.com/werman/noise-suppression-for-voice/releases/download/v1.10/linux-rnnoise.zip

Extract the archive's `linux-rnnoise/ladspa/librnnoise_ladspa.so` here. The upstream
plugin is GPL-3.0; its license is saved locally as `RNNoise-plugin-LICENSE`.
The downloaded binary is ignored by Git. It is not compiled from this project's code.

The gate uses the system's `zam-plugins` LADSPA plugin. Nothing here installs
system packages or modifies the PipeWire daemon configuration.

Downloaded artifact checksums (SHA256, recorded locally):

```
archive: 811390b6eb6e28dde023c70590c74d26e74ebb2e595bcf4b95af2341db160e99
plugin:  ba2b420c76cd2f1e7684430c96fef8c533041d5f67b94060f8eeda6af1701f97
```
