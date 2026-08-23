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

# T19 BuildTree Spike Telemetry

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
BuildTree timing shows high-magnitude frame-to-frame spikes, but current runtime telemetry only exposes total build time. We need phase-level and structure-level telemetry to identify spike drivers with evidence.

## Objective
Add BuildTree telemetry that reports per-phase timings and key structural/workload counters for each rebuild frame.

## Scope
- Add FMM build telemetry struct and capture in build path
- Expose telemetry through engine frame stats
- Show telemetry in runtime overlay for direct diagnosis
- Add regression assertions that telemetry is populated on rebuild frames

## Non-Goals
- Algorithm rewrite for FMM
- Asynchronous force-refresh behavior
- Performance tuning beyond telemetry instrumentation

## Target Files
- src/fmm_tree.hpp
- src/simulation_engine.hpp
- src/simulation_engine.cpp
- src/main.cpp
- tests/test_simulation_engine.cpp
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T19_buildtree_spike_telemetry.md

## Verification
- cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_COVERAGE=OFF
- cmake --build build-release
- ctest --test-dir build-release --output-on-failure -R simulation_engine
- IDE diagnostics for changed files

## Rollback
Revert this task commit to remove telemetry fields and UI output.

## Completion Artifact
- BuildTree telemetry visible in frame stats and overlay
- Regression checks proving telemetry is populated on rebuild frames

## Implementation Notes
- Added `fmm::BuildTelemetry` in `src/fmm_tree.hpp` with:
  - per-phase timings (`sort`, `node_lists`, `upward`, `downward`, `forces`, `total`)
  - structure counters (active nodes, height, leaves, max leaf occupancy, list sizes)
  - workload counters (`list_w_force_evals`, `direct_pair_evals`)
- Captured telemetry during each `buildTree()` execution and exposed it via `lastBuildTelemetry()`.
- Extended `sim::EngineFrameStats` with build-telemetry fields and update flag (`build_telemetry_updated_this_frame`).
- Wired `SimulationEngine::step` to copy telemetry into frame stats when a rebuild occurs.
- Extended overlay text in `src/main.cpp` to display build-phase timing and workload counters live.
- Added regression assertions in `tests/test_simulation_engine.cpp` that verify telemetry is populated on rebuild frames.

## Validation Notes
- IDE diagnostics: no errors in changed files.
- Command-level build/test execution is pending local run.
