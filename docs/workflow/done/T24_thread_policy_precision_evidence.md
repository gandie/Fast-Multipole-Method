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

# T24 Thread Policy Precision Evidence

## Metadata
- Owner: unassigned
- Created: 2026-08-23
- Updated: 2026-08-23

## Why Needed
Recent tree-build smoothing improvements changed execution policy. We need stronger proof that correctness and precision are preserved across thread-policy differences.

## Objective
Add targeted tests that verify FMM outputs are invariant (within strict numerical bounds) across single-thread and multi-thread build execution.

## Scope
- Add thread-policy invariance tests for FMM tree build.
- Assert structural telemetry invariants and force equivalence.
- Keep changes limited to test code and workflow bookkeeping files.

## Non-Goals
- Algorithmic changes to FMM implementation.
- Performance tuning changes.
- Broad test suite redesign.

## Target Files
- tests/test_fmm_accuracy.cpp
- docs/workflow/tasks/T24_thread_policy_precision_evidence.md
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T24_thread_policy_precision_evidence.md

## Verification
- cmake --build build-release -j
- ctest --test-dir build-release --output-on-failure

## Implementation Notes
- Added a new FMM test that compares single-thread and multi-thread builds on the same 1015-body deterministic grid fixture.
- The test asserts structural telemetry invariants are identical across thread policies:
  - active node count
  - tree height
  - leaf count and max leaf occupancy
  - near/interactions/list-W/list-X totals
- The test canonicalizes source-force pairs and checks per-body force agreement with strict mixed tolerances.
- The test additionally validates exact-force accuracy envelopes for both thread modes remain within existing conservative thresholds.

## Validation Notes
- IDE diagnostics for `tests/test_fmm_accuracy.cpp`: clean.
- Command-level build/ctest could not be run in this environment via available tools.

## Rollback
Revert the T24 test commit if it introduces unstable or invalid assertions.

## Completion Artifact
Thread-policy invariance tests merged and passing, with explicit evidence notes in changelog.
