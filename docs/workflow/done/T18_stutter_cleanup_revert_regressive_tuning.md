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

# T18 Stutter Cleanup Revert Regressive Tuning

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
Recent tuning changes intended to reduce build-time flicker increased stutter severity in runtime measurements. Regressive tuning must be removed to restore a cleaner and more stable baseline.

## Objective
Remove known regressive FMM tuning changes while preserving synchronous force-refresh semantics and previously safe stabilization fixes.

## Scope
- Revert runtime leaf occupancy change in SimulationEngine
- Revert max subdivision depth increase in FMM tree
- Revert force-loop scheduling change that can increase straggler variance
- Keep non-regressive safety and allocation-stability fixes from prior pass

## Non-Goals
- Reintroducing asynchronous force refresh
- Rewriting the integrator or FMM algorithm
- Adding new runtime configuration surface

## Target Files
- src/simulation_engine.cpp
- src/fmm_tree.hpp
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T18_stutter_cleanup_revert_regressive_tuning.md

## Verification
- cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_COVERAGE=OFF
- cmake --build build-release
- ctest --test-dir build-release --output-on-failure -R fmm
- ctest --test-dir build-release --output-on-failure -R simulation_engine
- IDE diagnostics for changed files

## Rollback
Revert this task commit to restore the regressive tuning if comparison runs require it.

## Completion Artifact
- Diff reverting regressive tuning constants/scheduling
- Diagnostics clean for changed files

## Implementation Notes
- Restored `SimulationEngine` FMM construction to `tree_(sources_, 60, 10)`.
- Restored `kMaxTreeDepth` to `20` in `FmmTree`.
- Restored force-loop scheduling to `#pragma omp parallel for schedule(dynamic, 64)`.
- Kept previous non-regressive fixes unchanged:
  - min-positive data-range padding
  - min root-box length floor
  - scratch-buffer reuse across rebuilds
  - near-field squared-distance floor

## Validation Notes
- IDE diagnostics report no errors in changed files.
- Command-level build/test and runtime-profile verification remains pending local execution.
