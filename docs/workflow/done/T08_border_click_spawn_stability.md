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

# T08 Border Click Spawn Stability

## Metadata
- Owner: unassigned
- Created: 2026-08-19
- Updated: 2026-08-19

## Why Needed
Clicking near screen borders during runtime particle injection can create unstable behavior, including apparent disappearance of bodies, which breaks interactive simulation reliability.

## Objective
Stabilize border-adjacent particle spawning and remove synchronization hazards in the add/remove + rebuild path so clicking near borders behaves predictably.

## Scope
- Diagnose runtime click-to-add behavior near borders
- Fix spawn position generation to avoid pathological boundary pile-ups
- Fix synchronization ordering in synchronous rebuild path used after particle count changes
- Add regression coverage for the border-spawn helper behavior

## Non-Goals
- Retuning core FMM/Barnes-Hut algorithm constants
- Redesigning simulation UX or input bindings
- Broad physics model changes beyond this defect

## Target Files
- src/simulation_engine.hpp
- src/simulation_engine.cpp
- tests/test_regressions.cpp
- docs/workflow/changelog/2026-08.md
- docs/workflow/tasks/T08_border_click_spawn_stability.md

## Verification
- cmake -S . -B build -DBUILD_TESTING=ON
- cmake --build build --config Release
- ctest --test-dir build --output-on-failure
- ./bin/sim

## Rollback
Revert T08 edits to restore previous spawn and rebuild behavior if unintended side effects are observed.

## Completion Artifact
Border-adjacent clicks no longer trigger pathological body behavior, and synchronous rebuild no longer risks source-lock join deadlocks.

## Implementation Notes
- Root cause analysis identified two compounding issues:
	- Border spawn used hard coordinate clamping, which can collapse many newly added particles onto the same boundary line/point and trigger unstable force spikes.
	- Synchronous rebuild joined the async worker while holding the sources mutex, which can deadlock if the worker is waiting on that same mutex.
- Added `src/spawn_utils.hpp` with bounded rejection sampling for click-spawn points.
- Updated `SimulationEngine::addParticles` to use bounded sampling rather than per-axis clamping.
- Follow-up hardening: sampling now uses a clamped in-bounds spawn center before radial sampling, and fallback returns a small bounded jitter instead of a single fixed point. This avoids corner-click degeneracy where many bodies could otherwise overlap exactly.
- Refactored add/remove paths to release sources lock before triggering synchronous rebuild.
- Updated synchronous rebuild to join worker before taking sources lock, then rebuild under consistent lock order.
- Added regression test ensuring border-adjacent sampled spawn points remain inside simulation bounds.
- Added regression test ensuring corner clicks do not collapse sampled points to one location.

## Verification Results
- IDE diagnostics: no errors in
	- `src/simulation_engine.cpp`
	- `src/simulation_engine.hpp`
	- `src/spawn_utils.hpp`
	- `tests/test_regressions.cpp`
- User-confirmed validation completed:
	- full test suite is green
	- runtime click behavior near all borders now remains stable

## Completion Notes
- Task objective met: border and corner click spawn behavior is stable, and the add/remove synchronous rebuild path avoids lock-order deadlock risk.
