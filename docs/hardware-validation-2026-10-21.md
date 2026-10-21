# Hardware workflow checkpoint — October 21, 2026 (Hawaii)

This is a partial acceptance record. The original executable was downloaded
from the physical console and verified. The owner reported a spinning prism
after launching it. No decoded NTR frame has been captured or inspected yet;
the owner observation is not screenshot evidence. No application or game data
has been written to the console in this session.

## Installed executable and local build

Application source baseline: `d68267ba316f62b9918112a0c38a5dbeea63637f`.
The installed files were found at
`/3ds/Colosseum3DS/3ds/Colosseum3DS/`, inside the supplied package directory.
The normal direct application path contained package documentation and that
nested directory. The files were preserved in place and backed up locally.

| File | Bytes | SHA-256 (installed and freshly built) |
| --- | ---: | --- |
| Colosseum3DS.3dsx | 181088 | `75d5c4b93781f68bf25f3cc8f58d49376d70e2564d793f3101eff3b4d0a167a8` |
| Colosseum3DS.smdh | 14016 | `aa4ea04bc11d878a77367dd4ac801367070b140108c7b16cedee49f83834daf6` |

The fresh build used `make -f Makefile.3ds -j2` in the local Docker image
`devkitpro/devkitarm@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5`.
Recorded packages: devkitARM r68-1 / GCC 16.1.0, libctru 2.7.0-1,
citro3d 1.7.1-2, Picasso 2.7.2-3, 3dstools 1.3.1-3.
Byte identity links the inspected installation to the reproducible baseline;
it does not establish any unobserved hardware behavior.

## Console acceptance

| Check | Result | Evidence / limit |
| --- | --- | --- |
| Passive FTP read | PASS | Both application files downloaded and SHA-256 checked |
| Launch and rotating geometry | PASS (owner observation) | Owner reported “its showing the spinning prism” |
| Native self-test | NOT TESTED | Bottom-screen result not received |
| Missing-data NOT READY | NOT TESTED | No bottom-screen frame |
| Real archive on console | NOT TESTED | No data deployment performed |
| A pause/resume and all buttons | NOT TESTED | No observed response |
| Circle Pad, C-stick, L/R, ZL/ZR | NOT TESTED | No diagnostic capture |
| START+SELECT exit and relaunch | NOT TESTED | Not yet confirmed |
| HOME suspend/resume and close | NOT TESTED | Test separately from NTR |
| Lid sleep/resume | NOT TESTED | Requires physical action |
| NTR video, both screens | NOT TESTED | Repeated TCP 8000 connection attempts timed out |
| Luma input redirection | NOT TESTED | No packets sent without visibility |
| Guarded deployment on console | NOT TESTED | Installed binary already matches fresh build; no replacement needed |

Console model: New 3DS family per handoff; exact model unverified.
Luma, Homebrew Launcher and ftpd versions: unverified. ftpd port was confirmed
on its screen as 5000; FTP greeting was `220 Hello!`, which does not identify
its version. BootNTR Selector was described in the handoff as Enhanced 3.6;
loaded engine/version remains unverified. NTRViewer-HR Lite v0.3.8.3 was fetched
from its official release and launched for capture. Its received-frame counters
were zero; no streaming session has been validated. No firmware, NTR installation
or system configuration changed.

The PC's Ethernet interface reached FTP. After the requested app launch,
FTP refused connections; ordinary ftpd is not assumed to remain active.
Private endpoint addresses, local full logs, backups and originals remain
in ignored local storage.

## Local validation and data availability

`make verify-source` verifies all eight exact pinned hashes after restoring LF
checkout bytes. Windows Git's CRLF conversion initially caused the hash check
to fail; the expected hashes and upstream commit were not changed.
The host C tests pass, including six FSYS/LZSS groups and 30 Pokémon checks.
The Python suite includes synthetic FTP failures; none of those tests establish
real hardware deployment or rollback support.
All 24 transfer tests pass, including renames applied before a lost reply,
readback mismatch, interrupted recovery and preservation of unknown active bytes.
All three C executables also pass AddressSanitizer and UndefinedBehaviorSanitizer
in the local GCC 13 container. This Python-only change has no repository lint or
typecheck configuration; Python compilation and `git diff --check` are used in
addition to the tests.

CI is independent: the historical run `39684365653` failed before runner steps,
with no diagnosed compiler failure. New PR check status is recorded on the PR;
the local build and hardware observations do not constitute a CI pass.

The private input repository was checked out at the pinned
`eae433c46b8b968dd7dcab72cf85351c8b9c7966`; `tools/verify_inputs.py` verified
26/26 originals. A local GC6E01 revision 0 extraction was found and
`prepare_data.py` successfully staged 656824337 bytes to a new local directory
outside the repository after checking disc identity and the canonical DOL hash.
The files have not been copied to the console.

An exploratory local ctypes harness compiled the unchanged `src/data.c` as a
shared library and decoded all 32 members of its 743136-byte `common.fsys`
with an 8 MiB per-member output limit. Every native output matched the pinned
upstream Python decoder. Decoded `common_rel` also matched the verified private
original. Archive SHA-256:
`f83b73c524498c1ec3ea1c04d06d391bcd6ca7e1ddd318fa33bcca0d6cf5b19a`.
This is host compatibility evidence, not a 3DS archive result. The exploratory
harness and outputs remain private; the committed synthetic tests are the
repeatable public regression suite.

## Repeat and next gate

Follow [hardware-workflow.md](hardware-workflow.md). First initialize the existing
BootNTR engine physically and establish fresh top/bottom frames; then test
controls with release and observed response. Identify the nested launch path
before any installation layout correction. Keep this PR focused on guarded
transfer tooling and the measured checkpoint; remote capture/control remains
unfinished until that physical gate can be resolved.

Next porting milestone: decode one explicitly identified real archive member
on the 3DS with an allocation ceiling and visible byte count/error status.
Acceptance: host/reference byte identity, console archive success and failure
diagnostics, bounded memory, preserved rendering/input, and fresh screenshots
of the exact tested build. A decoded REL is data/reference only and cannot run
as ARM code.
