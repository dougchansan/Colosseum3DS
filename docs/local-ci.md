# Local GitHub Actions runner

Both native jobs and the manual private-input verifier target
`[self-hosted, Windows, X64, colosseum3ds-local]` on a dedicated repository runner.
The checkout uses LF bytes so Windows CRLF conversion cannot invalidate pinned
source hashes. Jobs run Linux compiler/toolchain images through Docker Desktop;
the Windows runner does not require a native C compiler or a system devkitPro
installation. Every Docker exit code is checked before artifacts are published.

The host job uses `gcc:13` for regression and sanitizer checks. The ARM image is
pinned to the locally validated digest recorded in the workflow. The private
verifier retains its original pinned GHCR image and read-only source mount;
original game bytes are never uploaded as artifacts.

Jobs require a private repository, and PR jobs require a head branch from this
same repository. The runner has its own registration and working directory;
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
