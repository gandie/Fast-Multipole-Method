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

# T27 Metamorphic and Long-Horizon Guards

## Metadata
- Owner: unassigned
- Created: 2026-08-23
- Updated: 2026-08-23

## Why Needed
Even with existing threading and oracle checks, false trust can still arise from symmetry-law regressions and long-horizon replay drift that only appears at larger step counts.

## Objective
Add metamorphic invariance tests and an extended-horizon deterministic replay guard to harden physics and precision confidence.

## Scope
- Add FMM metamorphic tests for translation invariance and rotation equivariance.
- Add long-horizon scenario replay determinism guard.
- Keep changes limited to test suite and workflow bookkeeping.

## Non-Goals
- Any algorithmic changes to simulation runtime.
- Performance tuning changes.
- Rendering/UI checks.

## Target Files
- tests/test_fmm_accuracy.cpp
- tests/test_simulation_engine.cpp
- docs/workflow/tasks/T27_metamorphic_and_long_horizon_guards.md
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T27_metamorphic_and_long_horizon_guards.md

## Verification
- cmake --build build-release -j
- ctest --test-dir build-release --output-on-failure

## Implementation Notes
- Added FMM metamorphic tests in `tests/test_fmm_accuracy.cpp`:
  - `FMM direct mode is translation invariant`
  - `FMM direct mode is 90-degree rotation equivariant`
- Both metamorphic tests force single-leaf direct interaction (`max_sources_per_leaf = N + 1`) to isolate physics-law invariance from multipole approximation artifacts.
- Added deterministic long-horizon replay guard in `tests/test_simulation_engine.cpp`:
  - `scenario replay remains deterministic over extended horizon`
  - Uses `high_mass_ratio_binary` fixture for 20,000 steps at `dt=1e-3`.

## Validation Notes
- IDE diagnostics clean for:
  - `tests/test_fmm_accuracy.cpp`
  - `tests/test_simulation_engine.cpp`
- Command-level build/ctest was reported green by operator in this session.

## Rollback
Revert T27 test additions if they are flaky or scientifically invalid.

## Completion Artifact
Merged metamorphic and long-horizon determinism guards with documented tolerances and validation notes.
