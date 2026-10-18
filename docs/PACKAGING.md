# Debian packaging

SLRXFM ships a `debian/` directory (`debhelper-compat (= 13)`, CMake
buildsystem auto-detected by `dh`) that produces a real, installable
`.deb`.

## Building it yourself

The build needs Debian/Ubuntu-style tooling (`dpkg-buildpackage`,
`debhelper`, etc.) that isn't available on every development machine
this project is developed on (notably: not on Fedora, where this was
written). The straightforward way to build and check it anywhere is a
throwaway Debian container:

```sh
podman run --rm -v "$PWD/..":/work debian:trixie bash -c '
    apt-get update &&
    apt-get install -y build-essential debhelper cmake pkg-config \
        qt6-base-dev libsmbclient-dev libcurl4-openssl-dev lintian &&
    cd /work/slrxfm &&
    dpkg-buildpackage -us -uc -b
'
```

This produces `slrxfm_<version>_amd64.deb`, a `slrxfm-dbgsym_<version>_amd64.deb`
debug-symbols package, a `.buildinfo`, and a `.changes` file one level
above the source tree (`dpkg-buildpackage` convention).

## Build-Depends

- `debhelper-compat (= 13)`
- `cmake`, `pkg-config`
- `qt6-base-dev` (the GUI, `src/gui/`)
- `libsmbclient-dev` (real SMB backend)
- `libcurl4-openssl-dev` (real FTP/WebDAV backends)

If `qt6-base-dev`/`libsmbclient-dev`/`libcurl4-openssl-dev` are
missing, the upstream CMake build degrades gracefully rather than
failing outright (the GUI target and/or the affected network backend
is skipped with a CMake warning) — but the Debian package as packaged
here declares them as hard Build-Depends, since a `slrxfm` package
without the GUI or without working remote backends isn't the point of
shipping it.

## lintian results

Last checked 2026-10-18 against `slrxfm_0.1.0-1_amd64.deb` built in a
clean `debian:trixie` container, via `lintian slrxfm_0.1.0-1_amd64.changes`:

```
W: slrxfm: initial-upload-closes-no-bugs [usr/share/doc/slrxfm/changelog.Debian.gz:1]
```

That is the only warning left, and it is expected and correct at this
stage, not a packaging defect to fix: it means exactly what it says —
this changelog entry doesn't reference an ITP bug number yet, because
no real ITP has been filed (see `docs/ITP_DRAFT.txt`, which is
explicitly a draft for the human maintainer to file personally; filing
a real Debian BTS bug was out of scope for automation here). Once a
real ITP bug number exists, `debian/changelog`'s first entry should
gain a `Closes: #NNNNNN` line, which resolves this warning the normal
way every new Debian package does.

A previous pass also surfaced:

```
W: slrxfm: no-manual-page [usr/bin/slrxfm-gui]
```

Fixed by adding `docs/slrxfm-gui.1` (rendered and warning-checked
locally with `man --warnings`) and wiring it in via
`debian/slrxfm.manpages`.

No errors, and no other warnings, were produced by this lintian run.

## autopkgtest (`debian/tests/`)

`debian/tests/control` declares one test, `ctest`, run by
`debian/tests/ctest`. It uses `Restrictions: build-needed` (the test
binaries — `slrxfm_demo`, `slrxfm_gui_smoke_test`,
`slrxfm_transfer_worker_test` — are build-time-only, never installed
under `/usr/bin`) and `Depends: @, @builddeps@` so the testbed actually
has the build toolchain and runtime libraries (Qt6, libsmbclient,
libcurl) the build needs. That `Depends:` line matters in practice, not
just on paper: a first version of this test omitted it, and running
`debian/tests/ctest` against a testbed that only had `cmake` installed
failed immediately with missing `libsmbclient.so.0`/`libQt6Widgets.so.6`
— caught by actually executing the script against a minimal container,
not by reading it. With the declared `Build-Depends` installed, all
three CTest cases pass.

`dh_auto_test` (part of the ordinary `dh` sequence `debian/rules`
already invokes) also runs this same CTest suite automatically during
`dpkg-buildpackage`, independent of `debian/tests/`.

## What's intentionally not here yet

- No `debian/watch` — there's no tagged upstream release/tarball
  naming convention on GitHub yet for `uscan` to follow; add one once
  there's an actual release flow.
- Only one binary package (`slrxfm`) — no separate `-data` or `-dev`
  split; not warranted at this size.
