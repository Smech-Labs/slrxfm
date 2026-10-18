# SLRXFM — Smech Labs Remastered Xandros File Manager

A clean-room, from-scratch C++20 reimplementation of the *behavior* of
`XandrosFileManager`, the proprietary file manager shipped with Xandros
Desktop 1.0 (2002). Part of a broader historical-preservation and
software-recreation project for that distribution.

No original Xandros code, decompiled or otherwise, appears anywhere in
this repository. See [`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md) for the
full methodology, what was analyzed, what wasn't, and the specific
ambiguities that were deliberately left open rather than guessed at.

## What's here (v0.1)

Five modules modeling the subsystems identified in the first analysis
pass, each documenting in its own header which original class/function
names informed its scope:

| Module | Models |
|---|---|
| `src/directory_listing.{hpp,cpp}` | Unified local/remote browsable-location + capability model |
| `src/file_operations.{hpp,cpp}` | Bulk copy/move job, collision-checked rename/delete, create-subfolder |
| `src/trash_manager.{hpp,cpp}` | Move-to-trash / restore / empty-trash with sidecar metadata |
| `src/symlink_dialog.{hpp,cpp}` | Validate/accept controller for symlink creation |
| `src/network_browsing.{hpp,cpp}` | Real SMB (libsmbclient) / FTP (libcurl, MLSD) / WebDAV (libcurl, PROPFIND) backends behind one interface |

This is a modern target by design: C++20, `std::filesystem`, no legacy
Qt2/KDE2 API assumptions baked in.

On top of that logic layer, `src/gui/` is a real Qt6 GUI application
(`slrxfm-gui`): a folder tree + file-detail table driven by two
`QAbstractItemModel`/`QAbstractTableModel` adapters over the modules
above, with a toolbar/context menu and address bar wired straight into
`file_operations.hpp`/`trash_manager.hpp`/`symlink_dialog.hpp` — no
filesystem logic is reimplemented in the GUI layer itself. This part
is new UI engineering, not reverse-engineered; see
[`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md) for why that distinction
matters and where the line is drawn.

## Building

```sh
cmake -S . -B build
cmake --build build
./build/tests/slrxfm_demo                                  # logic-layer tests
QT_QPA_PLATFORM=offscreen ./build/tests/slrxfm_gui_smoke_test  # GUI, headless
./build/src/gui/slrxfm-gui [directory]                     # the actual app
```

The demo/test harness (`tests/demo_main.cpp`) exercises every logic
module against disposable scratch data under the system temp
directory. `tests/gui_smoke_test.cpp` does the same for the GUI under
Qt's offscreen platform plugin: it constructs the real `MainWindow`,
checks the list model populates from real directory contents, and
drives the actual New Folder and Rename toolbar code paths end-to-end.
Neither is part of the behavioral reconstruction itself — both are
ordinary test code.

Qt6 (`qt6-base-dev`/`qt6-qtbase-devel`), `libsmbclient`, and `libcurl`
are required for the full build; CMake degrades gracefully (skipping
the GUI target and/or the affected network backend, with a warning)
if any are missing.

## Documentation

- [`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md) — full analysis methodology,
  what was examined and how, what wasn't, and every ambiguity flagged
  rather than guessed at.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — design principles,
  module dependency graph, and the roadmap from "logic layer" to a real
  application.
- [`docs/PACKAGING.md`](docs/PACKAGING.md) — how the `debian/` package
  is built and the current `lintian` results.
- [`docs/ITP_DRAFT.txt`](docs/ITP_DRAFT.txt) — a draft Debian ITP bug,
  for the human maintainer to review and file personally.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — the clean-room discipline any
  new binary-derived module must follow, plus ordinary code-style/build
  notes.

## Status

Pass 1 covers `XandrosFileManager` only. Related components
(`CopyAgent`, `FtpAgent`, the `xsmb` CUPS backend, `libxfmwebdata.so`,
`libfmbookmarks.so`, `libpopupmenu.so`) and the Xandros SMB/network
wizard stack are candidates for future passes, each to be documented in
`docs/CLEAN_ROOM.md` following the same clean-room discipline.

This is now a **usable application**: a real Qt6 GUI, real SMB/FTP/
WebDAV backends, desktop integration (`.desktop` file, original icon,
AppStream metadata), and a real `debian/` package that builds and
passes `lintian` with only one expected warning. See
`docs/ARCHITECTURE.md`'s roadmap section for what's still open (mainly:
an async transfer worker instead of `BulkTransferJob` blocking the
caller, and `debian/tests`/autopkgtest wiring).

## Is this Debian-archive material?

Much closer than it was, and still worth being precise about what's
actually done versus what's still a human's job:

**Is the clean-room/licensing story sound?** Yes. GPLv3 keeps it
DFSG-free and eligible for `main` rather than `non-free` or an outright
rejection. The methodology in `docs/CLEAN_ROOM.md` exists specifically
so the "this isn't copied code" claim is verifiable, not just asserted.
The new GUI and SMB/FTP/WebDAV backends are ordinary new engineering
against public APIs (Qt6, libsmbclient, libcurl) — not
reverse-engineering, so they carry no clean-room concerns of their own,
and are documented as such.

**Is there a package here Debian would accept?** It builds, installs,
and runs:

- A real Qt6 GUI (`slrxfm-gui`) — folder tree, file listing, toolbar/
  context-menu actions, address bar — proven to run (not just compile)
  under `QT_QPA_PLATFORM=offscreen` and manually.
- Real SMB (libsmbclient), FTP (libcurl/MLSD), and WebDAV (libcurl/
  PROPFIND) backends — verified against live local test servers
  (pyftpdlib, wsgidav, a Samba container), not just compiled.
- Desktop integration: `.desktop` file, original SVG icon, AppStream
  metainfo, all validating cleanly (`desktop-file-validate`,
  `appstreamcli validate`).
- A `debian/` packaging directory that actually builds a `.deb` via
  `dpkg-buildpackage -us -uc -b`, with a man page, and passes
  `lintian` with exactly one warning (`initial-upload-closes-no-bugs`,
  expected until a real ITP bug number exists — see
  `docs/PACKAGING.md`).

What's left is the part that genuinely can't be automated: an `ITP`
(Intent To Package) bug filed under a real, accountable identity (a
draft is at `docs/ITP_DRAFT.txt`), a sponsor or DD to review and upload
it, and surviving the `NEW` queue review (ftpmasters checking
licensing, naming, and packaging quality by hand). There's also real,
but smaller, engineering debt: `BulkTransferJob` still runs
synchronously rather than on a worker thread, and there's no
`debian/tests`/autopkgtest wiring yet.

## License

GPLv3 (see [`LICENSE`](LICENSE)) — chosen specifically so this project
remains eligible for the Debian archive rather than risking non-free
classification or outright exclusion under a bespoke license.
