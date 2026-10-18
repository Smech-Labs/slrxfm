# Architecture

SLRXFM has two layers now: a **toolkit-agnostic logic layer**
(`src/*.hpp`/`*.cpp`, five modules modeling the subsystems identified in
`XandrosFileManager`, plus `tests/demo_main.cpp` exercising them against
real disposable filesystem state) and a **Qt6 GUI layer** on top of it
(`src/gui/`, see [Roadmap](#roadmap-toward-a-real-application) below for
what that does and doesn't cover yet).

## Design principles

- **One capability-queryable item model, not per-kind branching.** The
  original app's `CNetworkTreeItem`/`CListViewItem` hierarchy normalized
  local files, SMB shares, FTP sites, and bookmarks into one interface a
  view layer could query uniformly (`CanDoCopy`, `CanRename`, ...).
  `IBrowsableLocation` (`src/directory_listing.hpp`) keeps that idea:
  every location kind — local, SMB, FTP, WebDAV — implements the same
  four-method interface, so a UI never needs to know or care which kind
  of location it's showing.
- **Fail with a reason, not an exception.** Listing failures (permission
  denied, host unreachable, auth required) are expected, user-facing
  conditions, not exceptional program states — mirrored from the
  original's `error__12CViewManager(QString)` signal pattern. Every
  fallible operation here returns an explicit result/outcome type
  (`std::optional<std::vector<EntryInfo>>`, `FileOpResult`,
  `RemoteListingOutcome`, `SymlinkDialogResult`) instead of throwing.
- **Keep the out-of-process delegation shape where it was observed.**
  Copy/move were clearly delegated to an external `CopyAgent` helper
  process in the original (confirmed via `fork`/`execv`/`popen` imports),
  likely so a crash mid-transfer couldn't bring down the main window.
  `BulkTransferJob` (`src/file_operations.hpp`) keeps that shape
  conceptually — a real implementation would run it on a worker
  thread/process rather than call it synchronously, same as the
  original ran it out-of-process rather than inline.
- **UI-framework-agnostic by construction.** Nothing here references
  Qt, GTK, or any specific widget toolkit. Dialog-shaped logic (see
  `symlink_dialog.hpp`) is split into a presenter/controller that
  validates and produces an outcome, which any toolkit's dialog shell
  can wrap. This is a deliberate modernization: the original was tied
  to Qt2's `QListView`-item-subclassing approach, which doesn't map
  onto a modern `QAbstractItemModel`/GTK4 `GListModel` design anyway.

## Module dependency graph

```
directory_listing.hpp  (EntryInfo, ItemCapabilities, IBrowsableLocation, LocalDirectory)
        ^
        |
network_browsing.hpp   (RemoteSite : IBrowsableLocation; SmbShare, FtpSite, WebDavSite)

file_operations.hpp    (standalone — BulkTransferJob, rename_entry, delete_entry)
trash_manager.hpp      (standalone — TrashManager)
symlink_dialog.hpp     (standalone — SymlinkDialogController)
```

Only `network_browsing.hpp` depends on another module
(`directory_listing.hpp`, for `EntryInfo`/`ItemCapabilities`/
`IBrowsableLocation`). The other three are intentionally
self-contained — a consumer that only needs trash handling, say,
doesn't need to pull in the location model at all.

## Build layout

```
CMakeLists.txt        top-level: sets C++20, adds src/ and tests/
src/CMakeLists.txt     static library `slrxfm`
tests/CMakeLists.txt   `slrxfm_demo` executable, linked against `slrxfm`
```

Each module is a `.hpp` (all definitions — these are small, header-only
designs) plus a thin `.cpp` that exists only to give the module its own
compiled translation unit, matching conventional C++ library layout
even though nothing in these modules currently requires out-of-line
definitions.

## The GUI layer (`src/gui/`)

- `directory_tree_model.hpp` / `directory_list_model.hpp` — Qt6
  `QAbstractItemModel`/`QAbstractTableModel` adapters. They contain no
  filesystem logic of their own: the tree model lazily lists
  directories via `xfm::LocalDirectory::list_children()` (filtered to
  directories), and the table model shows whatever `IBrowsableLocation`
  it's pointed at. Both are new UI engineering, not reverse-engineered.
- `main_window.{hpp,cpp}` — the Qt6 `QMainWindow` shell: address bar,
  tree+table split, toolbar/context menu. Every action (new folder,
  copy/cut/paste, delete, rename, move-to-trash, empty-trash, create
  symlink) calls straight into `file_operations.hpp`/
  `trash_manager.hpp`/`symlink_dialog.hpp` — the GUI never reimplements
  that logic itself. `trigger_new_folder()`/`trigger_rename()` are
  test-only hooks that exercise the exact same code path as the real
  toolbar actions, used by `tests/gui_smoke_test.cpp`.
- `app_main.cpp` — the executable entry point (`slrxfm-gui`).

Overall layout (toolbar above a tree+detail split, address bar between
them) is an ordinary, generic file-manager convention; no Xandros
screenshot or shipped UI resource was consulted. See
`docs/CLEAN_ROOM.md` for why that distinction is drawn explicitly.

## Roadmap toward a real application

Done so far, roughly in the order `docs/ITP_DRAFT.txt`'s predecessor
(the old roadmap here) laid it out:

1. ~~A real UI~~ — done: Qt6, see [above](#the-gui-layer-srcgui).
2. ~~Real remote backends~~ — done: `SmbShare` (libsmbclient),
   `FtpSite` (libcurl, RFC 3659 MLSD), and `WebDavSite` (libcurl,
   RFC 4918 PROPFIND) all have real `do_list()` implementations in
   `network_browsing.cpp`, verified against live local test servers.
   The WebDAV backend is a generic client, not a verified
   reconstruction of `libxfmwebdata.so` — see `docs/CLEAN_ROOM.md`'s
   ambiguity #1, which that distinction is deliberately kept separate
   from.
4. ~~Desktop integration~~ — done: `data/slrxfm.desktop`, original SVG
   icon (`data/icons/slrxfm.svg`), AppStream metainfo, and a real
   `debian/` packaging directory that builds and passes `lintian` (see
   `docs/PACKAGING.md`).

Still open:

3. **An async transfer worker** — `BulkTransferJob::run()` is still
   synchronous/blocking, called directly from the GUI thread in
   `MainWindow::action_paste()`. A `std::jthread`-based worker with
   real progress reporting (the `ProgressCallback` plumbing already
   exists) would match the responsiveness the original clearly
   prioritized, and stop large copies from freezing the UI.
5. **`debian/tests`/autopkgtest** — `slrxfm_demo` and
   `slrxfm_gui_smoke_test` exist and pass but aren't wired into CTest
   (`enable_testing()`/`add_test()`) yet, so there's nothing for
   `dh_auto_test` or a future `debian/tests/control` to drive
   automatically.
6. **The actual Debian process** — an `ITP` bug filed for real (draft
   at `docs/ITP_DRAFT.txt`), a sponsor/DD, and the `NEW` queue review.
   Not something this repository's tooling can do on its own.

See the top-level README for how this ties into Debian-archive
eligibility specifically.
