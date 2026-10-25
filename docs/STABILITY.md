# Audio health and recovery

Open **Menu → Audio health**, or click the status strip's **Audio health** link.
It reads PipeWire's `pw-top` continuously through one persistent monitor. It does
not change the system sample rate, buffer size, scheduling or device routing.

- **Frames / Hz / ms per period** describe the busiest running audio driver's
  scheduling period. This is not microphone-to-headphone or network latency.
- **Busiest driver load** is PipeWire's W/Q ratio expressed as a percentage of
  the processing period. Node rows show their own processing load (B/Q for
  followers). These are audio processing loads, not total computer CPU usage.
- **Errors since reset** start after a two-second startup baseline. The Node counter column
  shows PipeWire's existing counters, which can include errors from earlier use.
  Idle/suspended placeholders are ignored so historical errors are not counted again.
  One interruption may increment several nodes; totals are not a count of audible
  clicks. Reset affects only the displayed session counters, not PipeWire.
- The table includes mixer nodes and the selected microphones/hardware outputs.
  Driver timing can come from another active audio graph; it is labelled as the
  busiest driver rather than presented as the latency of every mixer strip.
- The memory-lock limit is the controller's current process limit. Existing
  plugin workers may have different limits. Restart after changing login limits.

Short audio-control/discovery commands have a five-second timeout. A stuck
command is killed so the command queue and later refreshes can continue.

Each plugin rack has its own heartbeat. A frozen editor/worker or stalled audio
callback causes that worker to be stopped after roughly 15–20 seconds without
progress. Initial plugin loading and explicit plugin state/editor operations get up to a minute. The selected input falls
back to its normal path; other racks and the controller stay running. There is
no automatic plugin restart loop. Click **Plugins** on the failed input to retry.
If a plugin hangs while saving state, the last completed saved state is retained.

Plugin state is serialized periodically only when the plugin reports a parameter
or state change, and when the rack is changed or shut down. State APIs run on
the GUI thread without holding the rack lock needed by the audio callback. This
avoids blocking audio progress while a bridged plugin completes a state request.
A third-party plugin can still block inside its own state API; operation timings
are recorded in its host log for diagnosis.
Previous large host logs are rotated on the next launch.

For crackling, check whether the session error counts rise at the same time,
which input/bus is affected, the driver buffer timing, and whether bypassing a
plugin helps. These diagnostics make faults visible; they cannot guarantee that
a particular plugin or audio device will never underrun.

## Developer checks

`ctest --test-dir build --output-on-failure` checks DSP, release selection,
update failure/size handling, statistics parsing and command timeouts.

`python tests/verify_stability_private.py` requires Xvfb and runs a separate
PipeWire daemon, temporary profile and private display. It freezes a test plugin
worker and verifies normal routing recovery without touching desktop audio.
Pass `--xvfb /path/to/Xvfb` if it is not on PATH, and `--screenshots /output/path`
to capture both dialogs. No network access is needed.
