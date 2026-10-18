# Clean-room methodology

SLRXFM is a from-scratch reimplementation of the *observed behavior* of
Xandros Desktop 1.0's `XandrosFileManager` (2002), part of a broader
historical-preservation and software-recreation effort for that
distribution. It is explicitly **not** a port, decompilation output, or
transcription of the original binary.

## Why this matters

`XandrosFileManager` is a proprietary, closed-source Xandros component.
No source code for it has ever been published. Any legitimate
reimplementation has to draw a hard line between:

1. **Analysis** — understanding what the original program does, via
   disassembly/decompilation of the binary.
2. **Implementation** — writing new code that reproduces that *behavior*,
   without copying any code, structure, or expression from the
   decompiler's output.

This repository only ever contains the output of step 2. No decompiled
pseudocode, disassembly listing, or near-verbatim code fragment from the
analysis phase was ever written into this source tree. All logic here
was authored fresh, in modern C++20, organized around the *roles* the
original components played rather than their specific implementation.

## Analysis method

`XandrosFileManager` ships stripped (no `.symtab`), so there are no
function or variable names available directly. Two independent evidence
sources were used instead, deliberately preferring the cheaper one first:

- **String-table and RTTI evidence** (`strings`, `readelf`, `objdump`):
  this is Qt2-moc/old-GCC-era code, and its `.rodata` section still
  carries mangled `Class::method`-shaped strings (RTTI/exception-table
  artifacts) and Qt meta-object signal/slot signature strings. That gave
  a fairly complete architectural picture — class names, method names,
  call relationships — without decompiling most of the binary's 6,622
  procedures.
- **Targeted Ghidra decompilation** (via the
  [REA](https://github.com/morluto/rea) MCP tooling), used sparingly —
  exactly one function (the ELF entry stub) was decompiled in this pass,
  to confirm it's an ordinary `_start → __libc_start_main(main, ...)`
  glibc startup with no custom logic.

This means the architectural picture below is high-confidence on
*naming and call relationships*, but the vast majority of the binary's
actual branch-level logic was never decompiled or read, and so could not
have been copied even accidentally.

## Subsystems identified (pass 1)

- **UI mediator architecture**: `CMainFrame` (top window) →
  `CViewManager` (mediator owning the active view, KParts activation,
  clipboard/paste state) → `CLeftTreeView` (folder tree) + `CRightPanel`
  (detail/icon pane).
- **Item/location model**: `CListViewItem`/`CNetworkTreeItem`
  polymorphic hierarchy normalizing local files, SMB shares, FTP sites,
  Trash, and bookmarked web pages into one capability-queryable
  interface (`CanDoCopy`, `CanRename`, `CanMoveToTrash`, etc.).
- **File operations**: copy/move delegated to an external `CopyAgent`
  helper process (the binary imports `fork`/`execv`/`popen`); delete/
  rename are in-process with name-collision checks.
- **Trash/"Dumpster"**: `~/Desktop/Trash` directory + `CTrashEntryInfo`
  metadata, empty/restore operations.
- **Remote browsing**: SMB via `CSMBFileInfo`/`mountNetworkShare`, FTP
  via `CFtpSiteItem`/external `FtpAgent` helper. A WebDAV hook
  (`slotXFMWebDataFinished`) was found, but its real implementation
  lives in a sibling shared library (`libxfmwebdata.so`), not analyzed
  in this pass.
- **Symlink creation dialog**: `CSymLinkDialog`/`CSymLinkDialogData`.

## Mapping to this repository

| Original role (evidence-based name) | SLRXFM module |
|---|---|
| `CNetworkTreeItem`/`CListViewItem` hierarchy | `src/directory_listing.{hpp,cpp}` |
| Copy/Cut/Delete/Rename handling | `src/file_operations.{hpp,cpp}` |
| `CTrashItem`/Dumpster | `src/trash_manager.{hpp,cpp}` |
| `CSymLinkDialog` | `src/symlink_dialog.{hpp,cpp}` |
| SMB/FTP/WebDAV site browsing | `src/network_browsing.{hpp,cpp}` |

Every module's own file header documents exactly which original
class/function names were used as evidence for that module's scope, and
flags where the modern design deliberately departs from the legacy
approach (e.g. preferring `std::filesystem` and a toolkit-agnostic
interface over direct `QListViewItem` subclassing).

## Open ambiguities (not guessed away)

1. **WebDAV behavior** is inferred only from one hook name
   (`slotXFMWebDataFinished`), not verified against the real
   `libxfmwebdata.so` implementation. `WebDavSite` in
   `network_browsing.hpp` is a plausible placeholder shape, not a
   verified reconstruction — it currently fails closed, and its
   bundled comment says so.
2. **"Trash" vs. "Dumpster"** both appear as naming in the string
   table for what looks like one mechanism (`slotEmptyDumpster` and
   `slotEmptyTrash` both exist). `trash_manager.hpp` models this as one
   mechanism with two historical user-facing names, based on a single,
   non-dual, capability surface — not fully disambiguated.
3. **`CopyAgent`/`FtpAgent` IPC protocol** (one-shot `argv` invocation
   vs. a persistent pipe) is inferred only from `fork`/`execv`/`popen`
   imports, not from a decompiled call site.

## Not yet analyzed

`CopyAgent`, `FtpAgent`, the CUPS backend `xsmb`, `libxfmwebdata.so`,
`libfmbookmarks.so`, `libpopupmenu.so`, and the large majority of
`XandrosFileManager`'s own ~6,600 procedures remain unanalyzed. Coverage
here comes from string/symbol-table evidence (good for naming and
call-relationship confidence), not from exhaustive decompilation.

Future analysis passes against these components should follow the same
rule this document describes: analyze first, write fresh code second,
never let the two phases blur.
