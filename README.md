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

## Status

Pass 1 covers `XandrosFileManager` only. Related components
(`CopyAgent`, `FtpAgent`, the `xsmb` CUPS backend, `libxfmwebdata.so`,
`libfmbookmarks.so`, `libpopupmenu.so`) and the Xandros SMB/network
wizard stack are candidates for future passes, each to be documented in
`docs/CLEAN_ROOM.md` following the same clean-room discipline.

## License

GPLv3 (see [`LICENSE`](LICENSE)) — chosen specifically so this project
remains eligible for the Debian archive rather than risking non-free
classification or outright exclusion under a bespoke license.
