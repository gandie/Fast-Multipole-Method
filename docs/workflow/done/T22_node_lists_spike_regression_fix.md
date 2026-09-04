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

# T22 Node-Lists Spike Regression Fix

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
Post-T21 telemetry shows severe node-lists phase spikes with unchanged workload counters, indicating instrumentation/scheduling regression in BuildTree node-lists phase.

## Objective
Remove regression-inducing node-lists instrumentation overhead and restore less jitter-prone node-lists scheduling while preserving useful telemetry output.

## Scope
- Remove intrusive per-phase thread-count probe from BuildTree hot path
- Restore node-lists loop scheduling from static back to dynamic
- Keep OpenMP telemetry fields populated using non-intrusive runtime values
- Remove ACTIVE wait-policy forcing from startup path

## Non-Goals
- Removing BTEL telemetry
- Reworking force kernel math
- Broad architecture changes

## Target Files
- src/fmm_tree.hpp
- src/main.cpp
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T22_node_lists_spike_regression_fix.md

## Verification
- IDE diagnostics for changed files
- Runtime BTEL comparison around spike windows

## Rollback
Revert this task commit to reapply T21 behavior.

## Completion Artifact
- Node-lists phase no longer includes probe-induced overhead
- BTEL still reports OpenMP runtime fields

## Implementation Notes
- Removed `captureActiveThreadCount()` from `src/fmm_tree.hpp`; this helper created extra parallel regions inside timed BuildTree phases.
- Node-lists scheduling changed from `schedule(static)` back to `schedule(dynamic, 64)`.
- OpenMP telemetry fields (`omp_threads_*`) remain populated via non-intrusive `omp_max_threads` snapshot.
- Removed forced `OMP_WAIT_POLICY=ACTIVE` from `src/main.cpp` startup path; kept `omp_set_dynamic(0)` and fixed thread-count setup.

## Validation Notes
- IDE diagnostics: no errors in changed files.
- Runtime BTEL verification remains pending local run.
