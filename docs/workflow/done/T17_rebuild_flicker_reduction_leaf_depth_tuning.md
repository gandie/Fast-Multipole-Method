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

# T17 Rebuild Flicker Reduction via Leaf/Depth Tuning

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
After T16 stabilization, runtime still showed high-magnitude build-time flicker. Additional reduction of near-field burst variance is needed in the synchronous rebuild path.

## Objective
Reduce large rebuild-time spikes by lowering dense-leaf direct-interaction burst risk through tree subdivision tuning.

## Scope
- Increase maximum subdivision depth limit in FMM tree
- Reduce runtime target max sources per leaf in simulation engine
- Keep synchronous rebuild cadence semantics unchanged

## Non-Goals
- Introducing asynchronous force refresh
- Changing integrator algorithm
- Adding new CLI options in this pass

## Target Files
- src/fmm_tree.hpp
- src/simulation_engine.cpp
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T17_rebuild_flicker_reduction_leaf_depth_tuning.md

## Verification
- cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_COVERAGE=OFF
- cmake --build build-release
- ctest --test-dir build-release --output-on-failure -R fmm
- ctest --test-dir build-release --output-on-failure -R simulation_engine
- IDE diagnostics for changed files

## Rollback
Revert this task's commit to restore prior leaf/depth parameters.

## Completion Artifact
- Diff showing depth/leaf tuning
- Diagnostics clean for changed files

## Implementation Notes
- Added `kMaxTreeDepth = 28` constant and used it in `FmmTree::sortTree` leaf decision.
- Updated `SimulationEngine` FMM construction to use `max_sources_per_leaf = 32` instead of `60`.
- No change to force-refresh scheduling semantics in `SimulationEngine::step`.

## Validation Notes
- IDE diagnostics report no errors in changed files.
- Command-level build/test profiling is pending local execution.
