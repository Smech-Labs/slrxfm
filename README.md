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
| `src/file_operations.{hpp,cpp}` | Bulk copy/move job, collision-checked rename/delete |
| `src/trash_manager.{hpp,cpp}` | Move-to-trash / restore / empty-trash with sidecar metadata |
| `src/symlink_dialog.{hpp,cpp}` | Validate/accept controller for symlink creation |
| `src/network_browsing.{hpp,cpp}` | SMB / FTP / WebDAV async listing abstraction |

This is a modern target by design: C++20, `std::filesystem`, no legacy
Qt2/KDE2 API assumptions baked in. A real UI would sit on top of this
(Qt6 `QAbstractItemModel`, or GTK4 `GListModel`) — these modules model
the toolkit-agnostic logic layer only, not a finished application.

## Building

```sh
cmake -S . -B build
cmake --build build
./build/tests/slrxfm_demo
```

The demo/test harness (`tests/demo_main.cpp`) exercises every module
against disposable scratch data under the system temp directory and
asserts the expected behavior — it is not part of the behavioral
reconstruction itself, just ordinary test code.

## Documentation

- [`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md) — full analysis methodology,
  what was examined and how, what wasn't, and every ambiguity flagged
  rather than guessed at.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — design principles,
  module dependency graph, and the roadmap from "logic layer" to a real
  application.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — the clean-room discipline any
  new binary-derived module must follow, plus ordinary code-style/build
  notes.

## Status

Pass 1 covers `XandrosFileManager` only. Related components
(`CopyAgent`, `FtpAgent`, the `xsmb` CUPS backend, `libxfmwebdata.so`,
`libfmbookmarks.so`, `libpopupmenu.so`) and the Xandros SMB/network
wizard stack are candidates for future passes, each to be documented in
`docs/CLEAN_ROOM.md` following the same clean-room discipline.

This is a **logic layer, not yet a usable application** — there is no
GUI. See `docs/ARCHITECTURE.md`'s roadmap section for what's missing.

## Is this Debian-archive material?

Not yet, and it's worth being precise about why. Two separate questions
are easy to conflate here:

**Is the clean-room/licensing story sound?** Yes, as far as it goes.
GPLv3 keeps it DFSG-free and eligible for `main` rather than `non-free`
or an outright rejection. The methodology in `docs/CLEAN_ROOM.md` exists
specifically so the "this isn't copied code" claim is verifiable, not
just asserted — every module documents the exact string/symbol evidence
that informed it. That groundwork is the part that's actually done.

**Is there a package here Debian would accept?** No — and licensing
cleanliness doesn't change that. What exists today is five C++ logic
modules and a test harness, not an application a user could run. Debian
packages are judged on being genuinely useful, maintained software, not
on provenance alone. Concretely still missing:

- An actual UI (Qt6 or GTK4) — see `docs/ARCHITECTURE.md`.
- Working remote backends (SMB/FTP/WebDAV are currently stubs).
- Desktop integration: a `.desktop` file, icon, MIME associations.
- A `debian/` packaging directory (`control`, `rules`, `copyright`,
  changelog) — none exists yet.
- The actual Debian process: an `ITP` (Intent To Package) bug, a
  sponsor or DD to review and upload it, and surviving the `NEW` queue
  review (ftpmasters checking licensing, naming, and packaging quality
  by hand).

So: right foundation, real distance left to travel. Worth tracking as a
concrete milestone once the UI and packaging exist, rather than
treating "GPLv3 + clean-room docs" as sufficient on its own.

## License

GPLv3 (see [`LICENSE`](LICENSE)) — chosen specifically so this project
remains eligible for the Debian archive rather than risking non-free
classification or outright exclusion under a bespoke license.
