# Calibrated console navigation

`tools/console_navigate.py` performs bounded controller actions only after a
uniquely recognized, fresh screen authorizes a private calibrated route. It does
not start a video stream, launch an application automatically, or replace
source/hardware qualification of the intended build.

## Frame sources

The existing command remains compatible:

```sh
python tools/console_navigate.py --config navigation.json --target target-state --output new-evidence
```

By default, `Frames` owns a UDP8001 receiver for an already active NTR stream.
It does not send a TCP8000 video-start command. Its receiver, classification,
route limits are unchanged. The admitted holds are0.1/0.15 seconds for existing
actions,0.4 seconds only for DOWN, and0.2 seconds only for A. The narrow A0.2
addition comes from the actual nine-action Willie dialogue calibration used on
5b hardware; it does not admit0.2-second directional/HOME actions or a general
hold range.

When an existing recorder already owns the video receiver, reuse its publication:

```sh
python tools/console_navigate.py --config navigation.json --target target-state --output new-evidence --live-dir recorder-live
```

`PublishedFrames` reads only `frames.json` and `both.png`; its constructor and
`close()` own no socket, receiver thread or open persistent file. Publication
mode opens no additional UDP video receiver or NTR TCP8000 connection. The
existing controller sender is still called for a recognized authorized route
edge. A state explicitly declaring `ftp_ready` retains the existing FTP5000
readiness probe in either mode. That bounded probe runs only for its configured
state after fresh screen recognition; a screenshot alone does not establish an
FTP handshake. The publication reader itself opens no sockets.

Both sources implement `snapshot()`, `error` and `close()`. The existing
`wait_state` requires both screens' counters to rise above the baseline, a stable
unique match, and a subsequent counter rise after the candidate snapshot. Existing
state settling, 12-second transition waits, 90-second overall limit and 13-state
iteration bound are preserved. This option does not admit private longer or
move-specific routes, uncalibrated key holds, or arbitrary resume points.

## Publication contract

The recorder must publish a 400x480 RGB-compatible PNG with top screen at
`[0,0,400,240]` and bottom screen at `[40,240,360,480]`. Metadata contains `top`
and `bottom` objects with timezone-aware ISO `received_utc`, a positive integer
`counter`, and integer `frame_id` from0 through255. Boolean values are not valid
counters or IDs.

The writer replaces PNG before metadata. Each snapshot reads metadata, records
metadata/PNG modification times, reads the PNG, rereads metadata, then rechecks
the PNG modification time. A changed metadata payload, changed PNG timestamp or
PNG newer than metadata causes a complete reread. No cached image is returned.
Both receive timestamps must be between now and one second old; future,
timezone-less, stale, malformed or missing publications fail closed.

There are at most12 attempts with10ms backoff for publication replacement races.
Windows sharing/access errors `winerror`5/32 are retried within the same budget.
On Windows only, the CRT may report `errno=EACCES(13)` with no `winerror`; that
exact shape is also retried. POSIX/Linux EACCES, other errno/winerror values,
missing files and malformed content remain immediate failures. Persistent
sharing conflicts exhaust the budget and fail; retries never weaken timestamp,
coherence or route-deadline checks.

## Observed failures and verification scope

An actual private route stopped after three released actions when a replacement
briefly denied access to frames.json. The first repair handled winerror5/32 but
missed the Windows Python CRT error shape. A second run stopped after seven
actions; a local exclusive-file-handle reproduction confirmed
`PermissionError(errno13, winerrorNone)`. The narrowed Windows-only classifier
addresses that observed gap while preserving fail-closed behavior.

A later receive-only stress check of the private source reader recorded4,329
snapshots over90.016 seconds with zero errors. It opened no receiver client and
sent no controller input. The reviewed reader SHA256 was
`7778b9afd45d73e69cad1ec580eaabab74812ec4f403603d696e7d50dcec90c4`.
This is evidence for that bounded file-reader run, not public-route hardware
acceptance, a console input receipt guarantee, or proof of arbitrary timing and
publication rates.

Offline tests cover both frame-source selection paths, constructor cleanup,
publication coherence, counter rises, Windows/Linux error classification,
bounded exhaustion, stale-after-retry refusal, no cached fallback, CLI failure
manifests and no socket/input calls for failed publication admission. Existing
navigation tests remain unchanged.

```sh
python -m unittest discover -s tests -p test_console_navigate.py -v
```

Keep screen templates, route configuration and console addresses private. Calibrate
from immutable actual screenshots for the intended build, using regions that
include the exact text/cursor and exclude changing clocks or counters. Require a
unique match and verify negative controls for unrelated menus and save screens.
The reader does not certify template identity or decide that a new binary matches
an old calibration. The integration owner must qualify the installed candidate,
active recorder, allowed starting state and bounded hardware route separately.
