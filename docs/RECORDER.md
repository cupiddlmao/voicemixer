# Tape recorder behavior

Implemented against VB-Audio's September 2026 Potato manual, recorder pages 45–46
and the recorder controls on pages 79–80, plus the official recorder guide:
https://voicemeeter.com/user-guide-recording-with-the-integrated-recorder/

## Tape desk

- Click the filename at the top to load audio; no recording starts automatically.
- The progress bar seeks within the loaded file. Rewind/fast-forward buttons move
  the cursor, and repeat while held. Play toggles playback/pause; Stop resets
  playback or finalizes the recording. REC starts a new recording, then toggles
  record/pause while keeping all segments in that same file.
- A1–A5/B1–B3 buttons beside the tape route playback directly to those buses.
  Playback gain is independent of recording level and is applied before each
  bus's master gain/limiter. Playback does not depend on a microphone strip send.
- Right-click the tape to open recording options. Menu also offers recorder
  options, replay from start and eject.
- File selection supports WAV/BWF, AIFF, MP3, M4A/MP4, MOV, AVI, WMA/WMV and other
  audio formats supported by the installed FFmpeg build. Video is not displayed.

## Recording options

- **Pre-fader inputs**: arm any of the eight input strips. Their original incoming
  signals are mixed to stereo before EQ, denoise, gate, compression, gain/mute,
  color or effects. A hardware mic/interface channel is mixed to mono and copied
  to both sides, matching this mixer's hardware-strip channel selection.
- **Post-fader bus**: choose one of A1–A5 or B1–B3. The recording contains that
  bus's output after its processing and master controls.
- **Composite**: click a bus's Normal/Composite mode button and patch its eight
  independent output channels to hardware/app input channels. Choose pre-fader or
  post-fader signals. This makes a genuine eight-channel PipeWire source, which
  the recorder can capture. A bus mode/patch change restarts the engine briefly.
  Ordinary buses are stereo; recording more than two channels requires Composite.
- **WAV, BWF, AIFF or MP3**. PCM options include 8/16/24-bit integer and 32-bit
  float. MP3 is stereo with 32–320 kbps; supported MP3 sample rates are at most
  48 kHz. Other formats support up to eight recorded channels.
- **Independent sample rate**: the file can use a different rate from the 48 kHz
  mixer. Encoding/resampling happens in FFmpeg, outside the real-time DSP graph.
- **Multitrack** also writes a mono file per recorded channel, suffixed
  `_Track1`, `_Track2`, etc., in addition to the main interleaved file.
- **Target folder and prefix**: automatic date/time filenames are generated when
  REC is pressed. Existing files are not overwritten. The Linux default folder
  is `Documents/Linux Mixer`, rather than the Windows Documents/Voicemeeter path.
- **Pre-record buffer**: the buffer limit defaults to 20 seconds, adjustable
  from 10–60 seconds. Only the selected pre-record duration is retained in a
  circular buffer. Select 0 seconds to disable history, or a duration up to the
  limit to prepend that recent audio when recording starts.
  The buffer runs only while the controller and audio engine are active. Nothing
  is written to disk until REC is pressed. Stop engine stops this capture too.
  Capture uses its own 2048-frame capture buffer; this does not set the live
  microphone/headphone buffer or add a recorder insert to those audio paths.
  A separate reader thread drains the capture pipe during UI stalls. Its queue
  holds up to two seconds of complete audio frames. If the controller falls
  behind further during recording, recording stops with an error rather than
  silently dropping samples. With recording stopped, obsolete queued audio is
  discarded and the input meter catches up.
- **Play on load**, **Loop**, **Playback gain**, and **Stop recording after
  duration** are saved options. A duration of 00:00:00 disables automatic stop.
  Record-pause time is excluded from the recorded duration.

Recording continues when the window is hidden to the tray. Quit/Stop finalizes
an active recording; allow the file encoder to finish before removing storage.
Source/file options are locked during recording to preserve its channel layout.

## Verification and scope

The isolated native recorder test generates known tones, mutes a processing
strip, and proves that pre-fader capture still contains the original stereo
signal. It checks preroll, same-file REC pause, automatic stop, WAV/BWF/AIFF/MP3,
24-bit and float output, independent file rate, all eight Composite channels and
mono-track exports. Playback tests cover play-on-load, pause, seek, loop, selected
bus audio and silence on an unselected bus. No physical audio devices are used.

```bash
QT_QPA_PLATFORM=offscreen ./build/linux-mixer --recorder-test
```

For a separate PipeWire daemon with no desktop/device connections, use
`python3 tests/verify_recorder_private.py`. This also supplies the DSP ports and
explicit stream links normally created by the desktop session manager.

This reproduces the documented recorder workflow using native Linux routing and
FFmpeg. It does not use VB-Audio's proprietary recorder/codec implementation or
implement Voicemeeter's Windows Remote API/MacroButtons interface. Codec metadata,
encoder padding and the native file dialog can differ. The mixer as a whole
still lacks other Potato features such as VBAN, ASIO, surround upmix modes,
external FX inserts and the full parametric master EQ; this is not a claim of
complete Potato parity.
