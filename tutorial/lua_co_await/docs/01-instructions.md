# 01 — Project Instructions

Generalized from paintsnownext's `.github/copilot-instructions.md`. These are
the conventions real iris-based projects follow; apply them when maintaining
iris itself or building a new project on top of it.

## Project structure

- All code, comments and documents are written in **English**.
- Core library source lives under `src/`; third-party libraries under `ref/`.
  In principle the core must not depend on anything beyond the standard
  library (iris itself is header-only and dependency-free).
- Additional source code is provided as **plugins**, one directory per plugin
  under `plugin/`, with the plugin source in `plugin/<name>/src/` and its
  third-party dependencies in `plugin/<name>/ref/`.
- Documentation is organized by plugin under `doc/`.
- Do not modify other directories on the project root.

## Coding guidelines

- One unified style (third-party libraries keep their original style):
  - **TABs** for indentation.
  - **camelCase** for variables, **PascalCase** for functions/classes.
  - English only.
- When referencing functions/variables from other plugins, make sure they are
  **dll-exported or inlined** — otherwise the cross-module link breaks in a
  shared-library build.
- Do **not** call `lua.syserror` in functions that own RAII objects / hold
  references. Return `optional_result_t<>` / `result_error_t` instead (see
  [03-architecture.md](03-architecture.md) for the Lua-side contract).
- Visual Leak Detector (VLD) was removed from the reference project because it
  conflicted with the engine (shutdown hangs inside its `onexit` path). Do not
  reintroduce VLD-specific integration.

## Git guidelines

Every commit message starts with one of `[DOC]` `[ADD]` `[MOD]` `[FIX]`
`[DEL]` followed by a space, then a short English summary:

```
[MOD] Upgrade to Lua 5.5.1.
[FIX] Fix stack counting on coroutines.
[DOC] Add AGENTS.md with project rules.
```

## Checklist for new code

- [ ] English only, TAB indentation, camelCase/PascalCase.
- [ ] No new external dependency in core code.
- [ ] Cross-module symbols exported or inlined.
- [ ] Error paths return `optional_result_t<>`, no `lua.syserror` with RAII.
- [ ] Clean exit verified (no leaks, no unfreed allocations).
