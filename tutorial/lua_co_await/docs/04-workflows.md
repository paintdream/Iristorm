# 04 — Workflows & Debugging Methodology

Generalized from paintsnownext's project skill
(`.github/skills/paintsnow-dev/SKILL.md`). Project-specific details (paths,
env vars, scripts) are omitted on purpose; the *methodology* transfers to any
iris-based project. For the full project skill, see the paintsnownext
repository.

## Build

- With CMake `GLOB`-based source lists, **new `.cpp` files require two
  builds** (the GLOB re-scan happens on the second configure/build) before
  they appear in the target.
- Debug/Release output lands in the build tree together with `lua.exe` and
  the Lua DLL, so each tutorial build directory is self-contained and runnable.
- Keep the `/EHa` MSVC flag wherever Lua stubs are compiled (`luaL_error`
  longjmps through C++ frames).

## Runtime toggles

- Read configuration from **environment variables at startup** (e.g.
  `PAINTSNOW_PASS_TIMING=1`); each toggle is cheap and off by default, zero
  overhead when off.
- Prefer a live tuning panel (ImGui or a command console) over env-var
  restarts for frequently-tuned values — but keep the env vars for the
  offline/automated path.

## Regression discipline (A/B comparison)

The offline path is the regression harness: capture N deterministic frames
(or run an offline script) and compare against a committed baseline.

- **Untouched paths must stay byte-identical** (`diffBytes=0`, `maxDelta=0`).
- An **intentional visual-equivalent change** gets a re-captured baseline
  (with an explicit tolerance), never a silent acceptance of diffs.
- Never trust a single green run on timing-sensitive code — re-run multiple
  times (see Heisenbug below).

## Timing

- Bracket each named stage with begin/end debug labels; a timing system that
  reads them back **asynchronously** (ring of GPU timestamps read on wrap,
  hundreds of frames later) reports per-stage cost with zero stall.
- New stages are timed automatically once they use the label pair — no
  per-stage wiring.

## Debugging methodology (bimodal / nondeterministic output)

Goal: locate **which stage** flips between two states, then **why**.

1. **Classify states** — capture many offline frames; diff each against a
   baseline; group into state A / state B.
2. **Locate the diff region** — map the differing pixels (bbox + density
   grid) to a screen feature; this tells you which subsystem is involved.
3. **Isolate the stage** — dump every intermediate (each pass output) in BOTH
   states and binary-compare; the FIRST stage that differs while all its
   inputs are identical is the buggy stage.
   - **Read formats carefully**: a R16F dump read as float32 gives garbage
     comparisons; use format-aware compares (2 bytes/px vs 4 bytes/px).
4. **Check the inputs of the buggy stage** — identical inputs + different
   output = a real sync/descriptor race inside that stage's read path.
5. **Check validation stderr for the bad-state run** — look for NEW warnings
   vs the good state; a "descriptor never updated" warning is a real clue
   even when it appears in both states (shader reads uninitialized data).
6. **Heisenbug trap** — any code change in the buggy stage (a printf, an
   extra host read) can shift a timing-sensitive race and make it "pass".
   Don't trust a single green run; re-run 10-20x and always revert
   diagnostics before concluding.

## Known pitfall classes (from real bugs)

- **Descriptor/pipeline state caching**: a guard that skips re-binding "when
  nothing changed" can leave a freshly-resolved pipeline drawing with an empty
  descriptor set. Always include the mesh/pipeline identity in the
  "bindings changed" check.
- **Block name vs instance name**: validation messages name the instance,
  binding resolves by block name — the same shader declaration has two names.
- **Serialization is a red herring**: if the offline path is strictly serial
  on one warp, execute-scheduling is not the sync point to blame; the race
  lives in GPU-side sync (barriers) or descriptor state.
- **Warmup frames perturb**: async pipeline builds add variable warmup before
  the captured frame; disabling the suspect node reduces (but does not
  eliminate) timing races.

## Where to find the project-specific skill

The complete, project-specific skill (exact scripts, env var table, RenderNode
checklists, RenderDoc notes) lives in the paintsnownext repository:
`.github/skills/paintsnow-dev/SKILL.md` and `.github/copilot-instructions.md`.
