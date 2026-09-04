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

# T23 Tree Build Spike Optimization

## Metadata
- Owner: unassigned
- Created: 2026-08-23
- Updated: 2026-08-23

## Why Needed
Frame-time stutters remain because occasional tree rebuild spikes block the next frame. We need to reduce build-time outliers while preserving simulation precision and correctness.

## Objective
Optimize adaptive quadtree build internals to reduce occasional rebuild spikes without changing physical correctness.

## Scope
- Analyze current tree build path and related telemetry.
- Implement minimal, evidence-driven optimizations in tree build internals.
- Keep behavior/outputs compatible with existing correctness tests.
- Validate with existing unit and regression tests.

## Non-Goals
- Parallel tree build redesign requiring new per-frame synchronization.
- Algorithmic behavior changes that trade away accuracy.
- Broad refactors outside tree build path.

## Target Files
- src/fmm_tree.hpp
- docs/workflow/tasks/T23_tree_build_spike_optimization.md
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T23_tree_build_spike_optimization.md

## Verification
- cmake --build build-release -j
- ctest --test-dir build-release --output-on-failure

## Implementation Notes
- Added adaptive per-phase thread selection in `FmmTree` using workload-aware thresholds.
- Added serial fallback for node-list, upward, downward, and force phases when workloads are small.
- Preserved force math, neighbor/list semantics, and expansion logic; only execution policy changed.
- Updated phase telemetry fields to report effective thread counts used after adaptation.

## Validation Notes
- IDE diagnostics for `src/fmm_tree.hpp`: clean.
- Workspace diagnostics report an environment/tooling issue unrelated to this change:
  - CMake minimum required version in this repository is 3.28, while environment provides 3.22.1.
  - Full build/CTest verification commands could not be executed in this environment.

## Rollback
Revert the tree-build internal optimization commit and restore prior implementation if regressions appear.

## Completion Artifact
A merged diff with reduced hotspot overhead in tree build internals and passing test suite evidence.
