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

# T15 Force Sync Per Tick and Remove Async Residuals

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
Recent test findings identified correctness risk from asynchronous force-refresh behavior. Force fields must be refreshed deterministically on each scheduled frame/tick (`rebuild-every`) with no async worker lag path.

## Objective
Audit and remove residual asynchronous force-calculation logic from the simulation engine while preserving deterministic synchronous force rebuild cadence.

## Scope
- Audit all engine force-build and force-swap paths for async remnants
- Remove async worker/pending-force mechanisms that are no longer used
- Keep deterministic synchronous rebuild cadence behavior intact
- Validate cadence behavior with targeted engine regression tests

## Non-Goals
- Changing numerical force law or integrator method
- Changing CLI semantics for `rebuild-every`
- Broad performance tuning outside force synchronization semantics

## Target Files
- src/simulation_engine.hpp
- src/simulation_engine.cpp
- tests/test_regressions.cpp
- tests/README.md
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T15_force_sync_per_tick_and_remove_async_residuals.md

## Verification
- cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_COVERAGE=OFF
- cmake --build build-release
- ctest --test-dir build-release --output-on-failure -R cadence
- ctest --test-dir build-release --output-on-failure -R determinism
- IDE diagnostics for changed files

## Rollback
Revert this task's commit to restore previous async scaffolding if needed.

## Completion Artifact
- Diff removing async force-builder scaffolding from engine
- Passing force-cadence regression tests demonstrating synchronous schedule behavior

## Implementation Notes
- Removed asynchronous worker scaffolding from `SimulationEngine`:
  - deleted `AsyncTreeBuilder` thread/pending-force state and methods
  - removed `force_swap.hpp` dependency from engine
  - replaced all source locking through a direct engine mutex helper (`lockSourcesScoped`)
- Kept `SimulationEngine::step` force rebuild contract unchanged: synchronous rebuild on scheduled `rebuild-every` frames, reuse on intermediate frames.
- Removed obsolete force-swap regression tests and include from `tests/test_regressions.cpp`.
- Updated `tests/README.md` to remove stale force-swap wording.

## Validation Notes
- IDE diagnostics show no errors for changed files:
  - `src/simulation_engine.hpp`
  - `src/simulation_engine.cpp`
  - `tests/test_regressions.cpp`
  - `tests/README.md`
- Command execution for CMake/CTest could not be performed in this tool session; command-level runtime validation remains pending local run.
