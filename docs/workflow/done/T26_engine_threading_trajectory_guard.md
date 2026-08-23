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

# T26 Engine Threading Trajectory Guard

## Metadata
- Owner: unassigned
- Created: 2026-08-23
- Updated: 2026-08-23

## Why Needed
We need an additional long-horizon guard that verifies thread-policy differences do not cause unacceptable physics drift at simulation-engine level over many timesteps.

## Objective
Add engine-level regression tests that compare serial and multi-thread trajectory envelopes and drift metrics on deterministic fixtures.

## Scope
- Add long-horizon engine tests comparing 1-thread vs multi-thread runs.
- Assert bounded differences in physical metrics (radius, separation, pseudo-energy drift).
- Keep changes limited to tests and workflow bookkeeping artifacts.

## Non-Goals
- Any algorithmic or runtime behavior changes.
- Performance optimization changes.
- Render/UI checks.

## Target Files
- tests/test_simulation_engine.cpp
- docs/workflow/tasks/T26_engine_threading_trajectory_guard.md
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T26_engine_threading_trajectory_guard.md

## Verification
- cmake --build build-release -j
- ctest --test-dir build-release --output-on-failure

## Implementation Notes
- Added a new engine-level scenario guard test in `tests/test_simulation_engine.cpp`:
  - `thread policies preserve long-horizon scenario envelopes`
- Added OpenMP runtime state helpers (`OmpState`, `OmpStateGuard`) to restore runtime policy after test execution.
- Added threaded scenario runner helper `runScenarioFixtureMetricsThreaded(...)`.
- The test runs two long-horizon fixtures (`high_mass_ratio_binary`, `close_approach_binary`) under:
  - single-thread (`OMP=1`)
  - multi-thread (`OMP=min(8, runtime_max)`)
- Guard asserts both:
  - physical envelope compliance for each mode (finite state, bounded drift/radius/pair-distance)
  - near-equality of trajectory summary metrics across thread policies.

## Validation Notes
- IDE diagnostics for `tests/test_simulation_engine.cpp`: clean.
- Command-level build/ctest was reported green by operator after integration.

## Rollback
Revert T26 test additions if they are flaky or scientifically invalid.

## Completion Artifact
Merged engine-level threading trajectory guard tests with documented thresholds and validation notes.
