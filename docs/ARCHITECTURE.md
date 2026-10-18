# Architecture

SLRXFM is currently a **logic layer**, not a finished application: five
toolkit-agnostic C++20 modules that model the subsystems identified in
`XandrosFileManager`, plus a test harness that exercises them against
real disposable filesystem state. There is no GUI yet — see
[Roadmap](#roadmap-toward-a-real-application) below.

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

## Roadmap toward a real application

What exists today is the behavioral/logic layer only. To become an
actual usable file manager, in roughly the order it'd make sense to
tackle:

1. **A real UI** — Qt6 (`QAbstractItemModel` wrapping `IBrowsableLocation`)
   or GTK4 (`GListModel`). This is the biggest missing piece.
2. **Real remote backends** — `SmbShare`/`FtpSite`/`WebDavSite` currently
   have stub `do_list()` implementations; wiring these to
   `libsmbclient`, `libcurl`, and a verified WebDAV implementation
   (see `docs/CLEAN_ROOM.md`'s ambiguity #1) is needed before remote
   browsing actually works.
3. **An async transfer worker** — `BulkTransferJob::run()` is currently
   synchronous/blocking; a `std::jthread`-based worker with real
   progress reporting would match the responsiveness the original
   clearly prioritized.
4. **Desktop integration** — a `.desktop` file, icon, MIME associations,
   and (if targeting Debian) a `debian/` packaging directory. None of
   this exists yet.

See the top-level README for how this ties into Debian-archive
eligibility specifically.
