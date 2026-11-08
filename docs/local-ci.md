# Local GitHub Actions runner

The host, capture and ARM jobs and the manual private-input verifier target
`[self-hosted, Windows, X64, colosseum3ds-local]` on a dedicated repository runner.
The checkout uses LF bytes so Windows CRLF conversion cannot invalidate pinned
source hashes. Jobs run Linux compiler/toolchain images through Docker Desktop;
the Windows runner does not require a native C compiler or a system devkitPro
installation. Every Docker exit code is checked before artifacts are published.

The host job uses `gcc:13` for regression and sanitizer checks. The dedicated
capture job uses a fresh `python:3.12-slim` Docker environment and installs
`requirements-capture-test.txt` (`Pillow==11.3.0`). It explicitly imports Pillow
and `Image`, prints the Pillow version, then runs `test_ntr_capture.py` verbosely.
Both image-publication tests therefore run with Pillow available; an import or
installation failure fails the job before optional skips can hide missing
coverage. Pillow remains optional for FTP tools, native builds and the general
host suite. Use the same import and test commands from the README when checking
capture locally. The ARM image is
pinned to the locally validated digest recorded in the workflow. The private
verifier retains its original pinned GHCR image and read-only source mount;
original game bytes are never uploaded as artifacts.

The host, capture and ARM jobs run whether the repository is public or private.
PR jobs require a head branch from this same repository, so fork PRs never reach
the local runner through these job conditions. Because a fork PR could edit the
workflow itself, keep the repository's fork-PR workflow approval set to require
approval for all outside contributors. Logs and the uploaded build artifact are
public when the repository is public: they contain only tracked-source output,
and original game bytes never enter these jobs. The manual private-input
verifier still requires a private repository. The runner has its own
registration and working directory;
existing runners registered to other repositories are left unchanged. Keep its
registration credentials and machine paths out of Git.

The runner process and Docker Desktop must be running for jobs to start. The
initial installation runs the listener in the current Windows user session;
it is not a boot service. After a reboot, start `run.cmd` from the dedicated
runner directory. Confirm the repository's Actions → Runners page shows it
online with `colosseum3ds-local`. A workflow queued for these labels will wait
when the listener is offline. No GitHub-hosted runner fallback is configured.

The manual private verifier also requires this repository's existing permission
to read the pinned private GHCR package. Selecting a local runner does not grant
that package permission.
