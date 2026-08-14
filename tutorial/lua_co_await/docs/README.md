# lua_co_await — Tutorial Docs

Systematic documentation for understanding, mastering and extending projects
built on Iristorm (iris). The content is distilled from real-world experience
accumulated in [paintsnownext](https://github.com/paintdream/paintsnownext)
(its `.github/copilot-instructions.md`, `.github/skills/paintsnow-dev/SKILL.md`
and `doc/`), generalized so it applies to any iris-based project.

## Docs

| Doc | Topic |
|---|---|
| [01-instructions.md](01-instructions.md) | Project layout, code style, git conventions |
| [02-design-principles.md](02-design-principles.md) | The four design principles (C++/Lua co-development, top-down concurrency, self-closing loop, script callback model) |
| [03-architecture.md](03-architecture.md) | Script execution contexts, the two-step async pattern, verified pitfalls |
| [04-workflows.md](04-workflows.md) | Day-to-day workflows and debugging methodology (generalized) |
| [05-framework-patterns.md](05-framework-patterns.md) | Lua/C++ interaction framework patterns overview |

## How to read

1. **Run the tutorials first.** `require("lua_co_await").new():run_tutorials()`
   shows the primitives (binding, async, warp, quota, read/write fences).
2. **Read 01 → 02 → 03** for the rules and the mental model: what belongs in
   Lua vs C++, how concurrency is structured, and how script callbacks must
   behave.
3. **Read 04** when maintaining a project: build quirks, regression
   discipline, debugging playbooks.
4. **Read 05** and open
   [tutorial/lua_event_framework](../../lua_event_framework) side by side —
   it is the runnable, minimal reference implementation of the framework
   patterns, and paintsnownext is the full-scale example.

## Relationship to the code tutorials

The `src/tutorial_*.cpp` modules demonstrate single primitives. These docs
explain how the primitives are **composed** in a real project: the execution
context model (03), the two-step async pattern (03), and the framework layer
that projects typically add on top of iris (05).
