# Contributing to SLRXFM

## The one rule that matters more than any other

This project exists to reimplement the *behavior* of a proprietary,
closed-source binary (`XandrosFileManager`, Xandros Desktop 1.0, 2002)
without ever copying its code. Read
[`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md) before touching anything
that originated from binary analysis.

If you are contributing a new module based on analysis of a Xandros
binary (via Ghidra/REA or any other disassembler/decompiler):

1. **Do the analysis in a separate pass from the implementation.**
   Understand the behavior first — write it down in plain language
   (extend `docs/CLEAN_ROOM.md`), not as code.
2. **Never paste decompiler output anywhere in this repository** — not
   in a commit, not in a comment, not as a "temporary" scaffold to
   clean up later. If you're unsure whether something you're about to
   write is too close to the decompiled shape, rewrite it more loosely
   rather than risk it.
3. **Document your evidence.** Each module's file header should name
   the original class/function names (recovered from strings, RTTI
   artifacts, or symbol tables) that informed its scope, the same way
   the existing five modules do. This is what makes the clean-room
   claim verifiable by someone else, not just asserted.
4. **Flag ambiguity instead of guessing silently.** If the binary
   evidence doesn't clearly resolve how something behaved, say so in
   the module's header and in `docs/CLEAN_ROOM.md`'s ambiguities list,
   and make a conservative, clearly-labeled choice — don't present a
   guess as a confirmed reconstruction.

Contributions that don't fit this pattern (bug fixes, build system
improvements, the eventual UI layer, packaging) don't need to touch any
of this — it only applies to code whose existence traces back to
analyzing a Xandros binary.

## Code style

- C++20, no legacy Qt2/KDE2 API assumptions baked into new code (even
  when modeling behavior that originally used them — see
  `docs/ARCHITECTURE.md`'s design principles).
- Prefer `std::filesystem` and explicit result/outcome types over
  exceptions for expected failure modes (permission denied, host
  unreachable, validation failure).
- Keep modules toolkit-agnostic. Nothing under `src/` should `#include`
  a Qt, GTK, or other widget-toolkit header.

## Building and testing

```sh
cmake -S . -B build
cmake --build build
./build/tests/slrxfm_demo
```

`tests/demo_main.cpp` is the test harness. New modules should get
corresponding test coverage there, exercised against disposable scratch
data (see existing tests for the pattern — everything runs under
`std::filesystem::temp_directory_path()` and cleans itself up).

## License

All contributions are under GPLv3 (see [`LICENSE`](LICENSE)), chosen
specifically to keep this project eligible for the Debian archive. Tag
new source files with `// SPDX-License-Identifier: GPL-3.0-or-later` at
the top, matching the existing files.
