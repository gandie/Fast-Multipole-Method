---
agent_evaluation:
  version: 1
  evaluator: human_operator
  evaluated_at: YYYY-MM-DD
  verdict: accepted
  would_delegate_similar_again: true

  score_scale:
    min: 1
    max: 5
    meaning:
      1: poor
      3: acceptable
      5: excellent

  outcome:
    correctness:
    scope_discipline:
    validation_trust:

  collaboration:
    ambiguity_handling:
    operator_load:
    trust_delta:

  notes: >
    HUMAN NOTES
---

# T07 Refactor Main Engine and Rendering Split

## Metadata
- Owner: unassigned
- Created: 2026-08-19
- Updated: 2026-08-19

## Why Needed
The current main loop mixes simulation engine responsibilities with rendering/event orchestration in one file, which makes future engine changes riskier and harder to test independently from SFML rendering behavior.

## Objective
Refactor the runtime architecture so src/main.cpp focuses on rendering/event integration, while simulation stepping and force computation are exposed through a dedicated engine interface.

## Scope
- Extract simulation engine state and update flow from src/main.cpp into a new engine module
- Keep rendering, camera/view, and event-handling responsibilities in src/main.cpp
- Define a clear interface between renderer-side code and engine-side code
- Preserve current runtime behavior and CLI options
- Update build wiring if new translation units are introduced

## Non-Goals
- Replacing SFML or redesigning UI interactions
- Rewriting Barnes-Hut/FMM algorithm internals
- Introducing new simulation features unrelated to separation of concerns
- Performance retuning beyond what is necessary for parity

## Target Files
- src/main.cpp
- src/simulation_engine.hpp
- src/simulation_engine.cpp
- CMakeLists.txt
- docs/workflow/changelog/2026-08.md
- docs/workflow/tasks/T07_refactor_main_engine_render_split.md

## Verification
- cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
- cmake --build build --config Release
- ctest --test-dir build --output-on-failure
- ./bin/sim --help

## Rollback
Revert this task's engine-split edits so src/main.cpp returns to the previous monolithic flow, and remove newly introduced engine-interface files from the build.

## Completion Artifact
Runtime still builds and runs with equivalent visual behavior, and src/main.cpp no longer owns core simulation engine stepping logic directly.

## Implementation Notes
- Added a new `sim::SimulationEngine` module to encapsulate:
	- source initialization and runtime state
	- async tree rebuild management and force swapping
	- interaction radius and mouse-driven particle mutation
	- per-frame integration and timing statistics
- Refactored `src/main.cpp` into renderer/event orchestrator responsibilities:
	- CLI parse + help output
	- SFML window/event loop
	- particle and box drawing
	- text overlay updates from engine-provided stats
- Updated CMake executable sources to compile both `src/main.cpp` and `src/simulation_engine.cpp`.

## Verification Results
- User-confirmed validation completed:
	- full project tests are green after the refactor
	- runtime behavior is stable with rendering still driven by `src/main.cpp`
	- engine/render split is active with simulation logic delegated to `sim::SimulationEngine`
- Additional diagnostics evidence:
	- no C++ diagnostics reported in `src/simulation_engine.hpp`
	- no C++ diagnostics reported in `src/simulation_engine.cpp`

## Completion Notes
- Task objective met: `src/main.cpp` now orchestrates rendering/events while engine responsibilities are encapsulated behind a dedicated interface.

## Plan
1. Identify the simulation-state/step boundaries currently mixed into src/main.cpp.
2. Introduce an engine module API for initialization, stepping, force updates, and source access.
3. Move engine logic out of src/main.cpp while preserving existing interaction semantics.
4. Reconnect rendering/event code in src/main.cpp through the new engine interface.
5. Build and run tests to confirm behavior parity.
