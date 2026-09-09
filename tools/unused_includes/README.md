# find_unused_includes

Brute-force unused-`#include` detection for this repo: comment out an include,
recompile, keep the finding only if it still builds.

## Why brute force

The usual tools don't fit this codebase. Visual Studio 2022's built-in
`#include` cleanup is an IDE feature and we build with **Build Tools** only (no
IDE installed). clang-tidy's `misc-include-cleaner` and clangd's unused-include
diagnostic both need a `compile_commands.json`, which MSBuild does not emit, and
both are heuristic — they flag headers whose symbols aren't *directly*
referenced, which misfires on macros, transitive template instantiation, and
headers included deliberately as a facade.

Asking the compiler is slower but exact. At the scale of a branch diff
(~200 includes) it finishes in a couple of minutes.

## Usage

The projects must have been built at least once — compile flags are read from
the MSBuild `.tlog`.

```
# what changed on this branch
python tools/unused_includes/find_unused_includes.py --changed-vs main

# specific files
python tools/unused_includes/find_unused_includes.py --files blk_engine/renderer/renderer.cpp

# the whole codebase
python tools/unused_includes/find_unused_includes.py --all

# ...then actually remove them
python tools/unused_includes/find_unused_includes.py --changed-vs main --apply
```

Findings land in `tools/unused_includes/.probe/findings.json`.

## The two rules that make results trustworthy

**A `.cpp` verdict is final.** Nothing includes a `.cpp`, so if the TU still
compiles without the include, removing it cannot affect anything else.

**A `.h` verdict is not.** Two separate traps:

1. Removals must be checked against every TU that pulls the header in, under
   each consuming project's own flags. Several headers here are not
   self-contained — `kbWidgetCBObjects.h` uses `std::vector` and `std::string`
   without including `<vector>`/`<string>` — so a header often won't even
   compile standalone, and a standalone check over-reports. When this was first
   run, screening `blk_core.h` alone claimed 4 unused includes; testing against
   its real dependents rejected 2 of them.

2. **Individually-safe header removals are not jointly safe.** Removing an
   include from one header deletes the transitive path another header was
   silently relying on. Probing seven headers in isolation and then applying all
   the results at once broke the build. The tool therefore accepts header
   candidates *cumulatively*, re-verifying every TU in every project after each
   acceptance, so the accepted set is jointly consistent by construction.

Header probing runs against a throwaway copy of the source tree
(`.probe/tree/`), never your working tree.

## Things it deliberately leaves alone

- **A `.cpp` including its own header.** It compiles without it only because
  something else drags the header in transitively — but that include is exactly
  what proves the header is self-contained. Reported under "keep", not removed.
- **Includes inside `#if`/`#ifdef`.** A passing probe may only mean the branch
  is inactive in this configuration.

## Caveats

- Runs one configuration at a time (`--config`, default `Debug`). `Release`
  changes `_DEBUG`/`NDEBUG`, so conditional code can differ.
- Header trials use `/Zs` (syntax check, no codegen) for speed; source trials do
  a full compile. Always run a real build of both projects in both
  configurations after `--apply`.
- Vendored code under `External/` is skipped by `--changed-vs`.
